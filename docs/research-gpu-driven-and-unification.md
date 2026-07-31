# Research — GPU-driven dispatch & csubagio's unification (the two coupled #7916 objections)

**Written:** 2026-07-31 · **Branch:** `feature/render-to-compositor` (stock Godot 4.8-dev; local tree is primary source).
**Scope:** pressure-tests two load-bearing claims in `docs/proposal-draft-compositor-render-layer.md` —
Divergence #1 / objection 1b (per-instance opt-in is "GPU-driven-compatible-by-construction") and the
csubagio "one composable pass" unification — against primary sources: the local fill/cull path, reduz's
GPU-driven design gist, and #7916's body + comments read directly via `gh api`.
**Builds on (does not restate):** `docs/adversarial-review-narrowed.md` Attack #1 (a/b/c).

---

## Verdicts up front

> **Investigation 1 (GPU-driven).** The proposal's claim is **half-right, and the wrong half is the load-bearing
> one.** In *today's* CPU fill path the per-instance routing tag is genuinely free — routing is already
> per-surface, and one more branch costs nothing (SUPPORTED, `render_forward_clustered.cpp:1145-1160`). But
> reduz's constraint is explicitly *forward-looking* ("that we want to implement in the future"), and against his
> actual GPU-driven design the tag is **NOT free-by-construction**: his GPU cull sorts the passing-objects list
> **"by shader type"** into one indirect draw list per material (reduz gist, verbatim below). A per-instance
> holdout tag forces that sort to become a *two-key* sort (shader type × holdout-flag), **splitting each
> material's single indirect draw list into two**. That is exactly the "materials dispatched, not geometry"
> batch-fragmentation reduz put the pass selector in the material to avoid. The saving grace the proposal *should*
> claim (but doesn't correctly) is narrower: the split is a **GPU-side partition of an already-per-object list**,
> not a CPU per-object cost — so it is *cheap-ish on the GPU*, not *free-by-construction*. The proposal's stated
> mechanism ("changes which list, not how the material dispatches… orthogonal") is **wrong as written**: under
> GPU-driven dispatch, *which list* and *how the material dispatches* are the **same axis** (the list IS the
> per-material indirect dispatch).
>
> **Investigation 2 (unification).** csubagio's actual argument is stronger than the paraphrase: he wants a
> refactor of Godot's *existing* pass-like features into **one composable "pass" primitive** that touches
> "scene, node, material, buffers, and sorting systems in uniform ways," and he explicitly predicts *flagging of
> objects for inclusion/exclusion* as the confusion the proposal walks into. **B∧C∧D can be largely expressed as
> a #7916-style pass — EXCEPT the caller-order requirement (C) and the per-instance holdout (the QbieShay +3
> ask), which are precisely the two things #7916's *material-based* model cannot express.** So the held-out list
> is **not a fully distinct primitive** (its buffer/format/depth-share halves are pure #7916), but it is **not a
> pure #7916 pass either**: it needs a per-object selector and a caller-order sort that #7916 lacks by design.
>
> **Coupling resolution (the crux).** Investigation 1 lands on the fragmentation side. That means the honest fix
> — move opt-in off the instance toward a material/`render_mode` directive so it survives GPU-driven dispatch —
> **is csubagio's unification.** Both objections resolve at once *if the proposal recasts the holdout as a
> #7916 "pass" whose members are chosen material-side*, and adds only the two things #7916 genuinely lacks
> (caller-order sort + transparent/held-out target). **This costs the "any StandardMaterial3D unchanged, no
> shader" selling point** — the single most important thing the user must decide.

---

## Investigation 1 — is per-instance list routing free under GPU-driven dispatch?

### 1a — How Godot's fill/cull actually routes TODAY (SUPPORTS the "free tag" claim, for the CPU path only)

Primary source: `RenderForwardClustered::_fill_render_list`,
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:922`.

Routing today is **per-surface, at fill time, keyed by material-derived flags** — not by pipeline identity.
The fill loop iterates the CPU-culled instance list (`p_render_data->instances`, `:954`), and for each surface
of each instance decides its list from `surf->flags`:

```
:1145  if (!force_alpha && (surf->flags & (FLAG_PASS_DEPTH | FLAG_PASS_OPAQUE)))  rl->add_element(surf);        // OPAQUE
:1149  if (force_alpha || (surf->flags & FLAG_PASS_ALPHA))  render_list[RENDER_LIST_ALPHA].add_element(surf);    // ALPHA
:1155  else if (... uses_motion ...)                        render_list[RENDER_LIST_MOTION].add_element(surf);   // MOTION
```

Those `FLAG_PASS_*` bits are set from the material at cache-build time in
`_geometry_instance_add_surface_with_material` (`:4192` ALPHA, `:4198-4199` OPAQUE/DEPTH). So the pass a surface
lands in is **already a per-surface routing decision made in the hot loop**. Adding

```
if (inst->render_layer)  render_list[RENDER_LIST_COMPOSITOR_LAYER].add_element(surf);   // and skip opaque/alpha
```

is **one more branch in a loop that already branches per surface** — genuinely free in the CPU path. The draft's
Divergence #1 / objection 1b are **correct about today's engine.**

Two caveats that matter for the sort side:

- The list's **sort key** (`sort.sort_key1/2`, `render_forward_clustered.h:507-532`) is packed
  `depth_layer : geometry_id : material_id : shader_id : priority` — deliberately ordered to *minimise
  shader/material/geometry rebinds* (comment `:520-522`). A compositor list would **discard** this key and sort
  by `render_layer_order` instead — fine for the CPU path (it's a separate list with its own comparator, exactly
  as the proposal says at draft §Engine-sketch), but note it means the holdout list forfeits the pipeline-rebind
  optimisation the packed key exists to provide. For a handful of held-out instances that's irrelevant.
- Instances reach the fill loop already **CPU-culled** (`p_render_data->instances` is the visible set from
  `renderer_scene_cull.cpp`). So today, "which list" is decided **after** culling, on the CPU, per surface. This
  is the world the proposal's "free routing tag" is true in — and it is the world that reduz's GPU-driven design
  **replaces.**

**1a verdict: SUPPORTED for the shipped CPU path.** The tag is free today. But reduz's objection is not about
today (below).

### 1b — reduz's INTENDED GPU-driven design (the constraint is forward-looking — and it bites)

reduz states the constraint twice in the #7916 body, verbatim (confirmed via `gh api …/7916`):

> (body, line 146) "An excellent property of defining this in the material is that this workflow **remains
> entirely compatible with GPU driven rendering (where materials are dispatched instead of geometry).**"
>
> (FAQ, lines 209-210) **"Q: Why is the pass assignment material based? A:** Besides being easier to implement
> (and good for usability) this is **fully compatible with GPU driven rendering (that we want to implement in the
> future).**"

Note the tense: *"that we want to implement in the future."* **The GPU-driven renderer does not exist in this
tree** — a grep of `servers/rendering/` finds no GPU-driven path, only a stray "indirect draw" *comment* at
`render_forward_clustered.cpp:1121`. So the constraint is an assertion about an **unbuilt** design. The primary
source for that design is reduz's own gist, *"GPU Driven Renderer for Godot 4.x"*
(https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5). Verbatim, the cull→dispatch mechanism:

> "The list of objects passing must be sorted **by shader type**, in an indirect draw list fashion."
>
> "This can be achievable relatively easily using a compute shader that **counts all the objects of each shader
> type first, then assigns their offset in a large array, then creates the indirect draw lists for each
> shader.**"
>
> "Objects are rendered in multiple passes to a G-Buffer (deferred) by **executing every shader in the scene
> together with their specific indirect draw list.**"

This is the whole ballgame. Under GPU-driven dispatch:

1. **The unit of dispatch is the shader/material, not the instance.** Each material gets exactly **one** indirect
   draw list, built on-GPU by a compute pass that groups the visible set **by shader type only** — "no mention
   of additional per-object attributes participating in the sorting key" (gist read).
2. **"Which list an instance draws in" and "how the material dispatches" are therefore the SAME axis.** In the
   CPU path they are separable (the proposal's "orthogonal" claim). In the GPU-driven path they are not: the list
   IS the per-material indirect dispatch batch.
3. **A per-instance holdout tag forces a second grouping key.** To honour "this StandardMaterial3D instance goes
   to the compositor list, that one to opaque," the GPU cull-sort must group by **(shader type × holdout flag)**,
   splitting each affected material's single indirect draw list into two. That is precisely the material-dispatch
   fragmentation reduz's material-based selector was designed to avoid — the material *itself* carries the pass,
   so the natural grouping key (shader type) already encodes the pass, and no split is needed.

**So the draft's stated mechanism is wrong.** Draft objection 1b says the tag "changes *which list* an instance
draws in, not how its material dispatches, so it is orthogonal to material-dispatched GPU-driven rendering."
Under reduz's design, changing which list an instance draws in **is** changing how its material dispatches
(it splits the dispatch batch). The words "orthogonal" and "does not fragment material dispatch" are false
against the primary source.

### 1c — but is the honest cost actually catastrophic? (the nuance that keeps the door open)

No — and this is where the draft *could* have a real, defensible argument, just not the one it made:

- The holdout tag can be carried **in the GPU-visible per-object buffer** (reduz's "large array" of objects,
  bindless model). The cull compute shader already reads each object to bin it by shader type; binning by
  (shader type × 1-bit holdout) is a **wider key on a pass that is already per-object on the GPU**. This is a
  *GPU-side partition*, **not** a CPU-side per-object pre-partition — so it does NOT reintroduce the per-object
  *CPU* cost reduz designed material-based assignment to avoid. The cost it reintroduces is **draw-list
  fragmentation / extra indirect draws**, not CPU dispatch overhead.
- Whether that is acceptable is a *quantitative* question reduz's gist does not answer (the gist "does not
  provide detailed pseudocode for the compute shader itself"). The GPU-driven design is **underspecified**
  upstream — which cuts both ways: reduz's "materials dispatched not geometry" objection is itself partly
  *speculative on an unbuilt renderer*, so the proposal cannot be *refuted* by it with certainty either. But the
  proposal cannot claim *compatible-by-construction* — the honest status is "adds a grouping key to an unbuilt
  GPU cull; cost unknown, plausibly small for few held-out instances, but it is a real batch split, not a
  no-op."

**1b/1c verdict: NOT free-by-construction.** Per-instance routing splits the per-material indirect dispatch
under reduz's design. It is *cheap-ish on the GPU* (a wider cull key, tag in the instance buffer) rather than
*expensive on the CPU*, but the proposal's "orthogonal / does not fragment material dispatch" language is
false and must be retracted. **If the batch split is judged unacceptable by the #7916 owners, the opt-in has to
move to a material directive — losing "any StandardMaterial3D unchanged, no shader authoring."**

---

## Investigation 2 — can B∧C∧D be a composable #7916 pass? (csubagio)

### 2a — csubagio's actual argument (full text, both comments, via `gh api`)

**csubagio, 2023-09-29 (+17):**

> "I abstractly think of this as being a kind of **'render pass'** which I define as a unit of rendering work
> that takes a scene and produces a set of buffers… a pass **filters which draw calls in the scene are used, in
> what order, and what configurations need to apply to the materials.** … rendering a shadow buffer, a z-prepass,
> a depth buffer occlusion test, an opaque deferred pass, a forward pass, a hard split between transparencies
> inside and outside of a cockpit, an intermediate blurring downsample step, and applying a combined
> tonemap/bloom, all fit into the same pass mechanism."
>
> "What raises a red flag… is that it doesn't unify all of these concerns into a single concept… engines I've
> worked on have ended up in a sticky situation when they find areas where overlap between these features cause
> redundant or even competing mechanisms… Then there's **flagging of objects/materials/groups for
> inclusion/exclusion in various sets of things that start becoming confusing to name in the UX.** It leads to the
> kind of *'Oh, that's a material pass and not a depth flag? Wait, why is this a material layer flag instead of a
> pass? Oh, because it's transparent?'* discussion where you're really just exposing the engine layout to the
> user, rather than the feature name."
>
> "**I would suggest… a refactor of the existing 'pass like' features of Godot, which would result in a new
> composable 'pass' primitive**, that could then also accommodate the new 'pass like' features you describe, in
> such a way that they touch the scene, node, material, buffers, and sorting systems in uniform ways."

**csubagio, 2023-09-29 (+7), replying to reduz:**

> "I'm not saying it should be more in the sense that it should be more complicated, I'm saying it should be
> **less** complicated, **less of a unique special case**, because it's actually something pretty fundamental…
> your proposal, sitting alongside everything else already in the engine, looks like complication to me. It also
> looks like the basis for **compounded future complication, which will come from incremental related community
> requests**."

The paraphrase in `adversarial-review-narrowed.md` §1c is faithful but understates two things: (1) csubagio's
"pass" is defined by **filter + order + material-config**, i.e. it *already contains* caller-order as a
first-class dimension ("in what order") — which is exactly requirement C the proposal needs; and (2) his named
failure mode ("why is this a material layer flag instead of a pass? Oh, because it's transparent?") is a
**near-verbatim prediction of this proposal's shape** (a per-instance/material flag whose reason-for-existing is
the transparent/held-out case).

### 2b — mapping B∧C∧D onto csubagio's pass

| B∧C∧D component | Expressible as a #7916 pass? | Expressible as a *csubagio* pass? |
|---|---|---|
| **B** shared-scene-depth occlusion | Yes — a pass reads resolved scene depth (#7916 custom-buffer/depth-share vocabulary covers it) | Yes (pass "takes a scene and produces buffers") |
| **B'** named/identity target | Yes — this is #7916's custom-buffer surface (darksylinc +28 even wants hashed-string names) | Yes |
| **C** caller-controlled order | **No** — #7916 explicitly: transparency "is sorted back to front… within a single render pass"; #7916 has no caller-order key | **Yes** — csubagio's pass defines "in what order" as a pass dimension |
| **D** in-frame seed-LOAD by the owning effect | Partly — a pass can LOAD a buffer; the seed↔composite coupling (Attack #3) is orthogonal | Yes |
| **Holdout member selection** (which instances) | **Material-based only** — #7916 selects pass membership via the *material* (`compositor_opaque_pass`), NOT per-instance | **Yes, but** csubagio warns *this is the exact flagging that "becomes confusing to name"* |

The table shows the split cleanly:

- **The buffer / format / depth-share / target halves of B∧C∧D are pure #7916.** They are *not* a distinct
  primitive; they are #7916's custom-buffer surface, which the draft already says it reuses. On these, csubagio
  is right — do not invent a parallel mechanism.
- **The two things that are NOT #7916 are exactly C (caller order) and per-instance holdout membership.** #7916
  is material-based and depth-sorted by construction. These are the draft's Divergence #1 + the instance-order
  key — and they line up with the *in-thread demand* the review already found:
  - **QbieShay (2025-03-20, +3), verbatim:** "I think **pass index should be overridable per-object,** if
    possible." → per-object membership is wanted even *inside* #7916's own thread.
  - **darksylinc (+28)** wants transparent/held-out passes at all ("Extreme disagree… render special geometry…
    the gun and hands should be rendered always in front"); **thygrrr:** "should most definitely **not** be
    limited to opaque passes only." → the transparent/held-out target is claimed territory inside #7916.

### 2c — verdict: a *pass with two extensions*, not a distinct primitive, not a pure #7916 pass

B∧C∧D is best described as **"a #7916 pass, plus (i) a caller-order sort and (ii) a per-object membership
override."** It is therefore:

- **NOT a genuinely distinct primitive** — its target/buffer/depth machinery is #7916's, and csubagio's "one
  composable pass" is the correct frame. Filing it as a standalone `RENDER_LIST_COMPOSITOR_LAYER` + per-instance
  flag + new resource is precisely the "compounded future complication / new unique special case" both csubagio
  comments warn against.
- **NOT a pure material-based #7916 pass either** — it needs caller-order (C) and per-object membership, the two
  axes #7916 lacks *by design*. QbieShay +3 shows the per-object axis has independent demand; darksylinc +28 /
  thygrrr show the transparent axis does. So the extensions are *justified inside #7916*, not novel.

---

## Coupling resolution (Investigation 1 ⇒ Investigation 2)

The brief's coupling fires. **Investigation 1 concluded per-instance routing is NOT free under GPU-driven
dispatch** (it splits the per-material indirect batch; "orthogonal" is false). The honest remedy for that is to
**move the holdout selector off the instance and onto the material** (a `render_mode` / material pass
directive) — which is *exactly* csubagio's unification and exactly #7916's existing membership model. So:

**Both objections collapse into one move.** Recast the proposal as **a #7916 pass** (membership chosen
material-side, GPU-driven-safe by reduz's own construction) that adds only the two things #7916 genuinely
lacks and its own thread already demands:

1. **a caller-order sort** for that pass (satisfies C; QbieShay's per-object-order intent; #3986/#11251
   ordering asks), and
2. **a transparent/held-out target** the effect seeds and composites in-frame (satisfies B/D; darksylinc +28 /
   thygrrr).

This answers csubagio ("it's one more configuration of the composable pass, not a new special case"), answers
reduz ("membership is material-based ⇒ GPU-driven dispatch unaffected"), and lands *inside* #7916 rather than
beside it — the repositioning `adversarial-review-narrowed.md` already recommends, now with a mechanism.

**The cost is explicit and unavoidable:** material-side membership **forfeits the draft's headline selling
point** — "any `StandardMaterial3D` participates unchanged, no shader authoring." A `StandardMaterial3D` would
need *some* material-level opt-in (a flag/`render_mode`/next-pass material), not a pure per-instance property.
QbieShay's "overridable **per-object**" ask (+3) is the counter-pressure: users *do* want per-object control.
The reconcilable design is a **material-based pass membership with an optional per-instance override** — but the
per-instance override is the part that re-incurs the GPU-driven batch split, so it must be documented as
"per-object override splits the material's GPU-driven dispatch batch; use sparingly," not sold as free.

### The single decision the user must make

**Keep per-instance opt-in (retain "any StandardMaterial3D unchanged," accept that it fragments reduz's future
GPU-driven material dispatch and that csubagio's unification objection stands) — or move membership material-side
(resolve BOTH reduz and csubagio at once, lose the no-shader-authoring selling point, and reposition the whole
proposal as a #7916 pass-with-caller-order rather than a standalone held-out list).** Investigation 1 says the
technical wind is behind the second option; the first option's "compatible-by-construction" defense does not
survive the primary source.

---

## Source ledger

- Local fill/cull path: `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:922`
  (`_fill_render_list`), `:954` (culled instance loop), `:1145-1160` (per-surface list routing), `:4192-4199`
  (`FLAG_PASS_*` set from material); sort key `render_forward_clustered.h:507-532`. No GPU-driven path exists in
  tree (grep `servers/rendering/`).
- #7916 body via `gh api repos/godotengine/godot-proposals/issues/7916`: GPU-driven quote line 146; FAQ
  "pass assignment material based" lines 209-210; "not much of a point… transparent passes" (FAQ).
- #7916 comments via `gh api …/7916/comments --paginate`: csubagio 2023-09-29 (+17) and (+7);
  QbieShay 2025-03-20 (+3) "pass index should be overridable per-object"; darksylinc (+28) / thygrrr per
  `adversarial-review-narrowed.md` §1b/1d.
- reduz, *GPU Driven Renderer for Godot 4.x* (design gist, unbuilt):
  https://gist.github.com/reduz/c5769d0e705d8ab7ac187d63be0099b5 — "sorted **by shader type**", compute shader
  "counts all the objects of each shader type… creates the indirect draw lists for each shader," "executing
  every shader… with their specific indirect draw list." Design underspecified (no cull pseudocode).
