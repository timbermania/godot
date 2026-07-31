# Adversarial review — the *narrowed* compositor render-layer proposal

**Written:** 2026-07-31 · **Reviewer stance:** skeptical Godot rendering maintainer, mandate to *reject or narrow*.
**Target:** the current (narrowed) `docs/proposal-draft-compositor-render-layer.md` +
`docs/render-to-compositor-interface-design.md`.
**Predecessor:** `docs/adversarial-review-subviewport.md` (the SubViewport attack — *settled*; the proposal survives
only on the **B∧C∧D intersection**: shared-scene-depth occlusion ∧ caller-order ∧ in-frame seed-LOAD). This review
does **not** re-run that; it attacks the parts the prior session strengthened but never verified against primary
sources: **#7916's actual text**, the **Mobile** subpass architecture, the **property lifecycle**, the
**seed_source** semantics, and the **structural-invariant** claims.

**Method:** #7916 issue body + all 56 comments read directly via `gh api` (through 2026-07-10). Mobile and
lifecycle verified by two source-dive agents against the engine tree on branch `feature/render-to-compositor`
(stock Godot 4.8-dev). Line numbers below are as read on that branch.

---

## Verdict up front

> **The capability (B∧C∧D) still survives — but the primary-source read moves the honest recommendation from
> "file as a standalone counterpart to #7916" to "fold into #7916's orbit, and the standalone *packaging* is the
> part #7916's own thread argues against."** Two of the proposal's load-bearing claims fail against source: its
> headline divergence (**per-instance opt-in**) collides head-on with a *stated #7916 design invariant*
> (GPU-driven rendering ⇒ material-based dispatch), which the proposal never addresses; and its "capability-gated,
> both RD renderers" claim is **realistically Forward+-only** — a deferred held-out pass is structurally hostile to
> Mobile's tile-based subpass chain. One claim the proposal makes is *vindicated* by source (the property
> lifecycle — no chicken-and-egg), and one divergence is *supported* by a top #7916 comment (identity-over-index).
> Net: **narrow further and reposition as "wait-for / land-inside #7916," or fall back to the fork (ADR-0001).**

---

## Attack #1 — the #7916 sequencing bet (the ballgame). VERDICT: **mostly lands. The bet is worse than pitched.**

The proposal's viability rests on being "the transparent / held-out counterpart of #7916" that *reuses* its
conventions. Nobody had read #7916's body + comments this session. I did. State: **OPEN, 193 👍, updated
2026-07-10; only the `CompositorEffect` sub-part (#80214) ever shipped.** Author **reduz**; the active maintainer
voice in 2025–26 is **clayjohn**. Three findings, each undermining the "complementary, they'll welcome it" story.

### 1a — The headline divergence collides with a *stated* #7916 invariant, unaddressed (the strongest single hit)

The proposal's **Divergence #1** — "opt-in on the *instance*, not a material directive… what lets any
`StandardMaterial3D` mesh participate without a shader" — is presented as "argued, not accidental." But the
argument is *purely ergonomic*. It never engages the reason #7916 chose material-based dispatch, which reduz
states **twice**, as an architectural invariant:

> (body) "An excellent property of defining this in the material is that this workflow **remains entirely
> compatible with GPU driven rendering** (where materials are dispatched instead of geometry)."
>
> (FAQ) "**Why is the pass assignment material based?** … this is **fully compatible with GPU driven rendering**
> (that we want to implement in the future)."

Per-instance/per-geometry routing is *exactly* what a GPU-driven pipeline (materials dispatched, not instances)
cannot express cheaply — which is why reduz put the pass selector in the material. The proposal's central
differentiator is therefore not a neutral divergence to defend in a paragraph; it is a **direct collision with a
forward-looking architectural constraint the #7916 author named as load-bearing**, and the proposal is silent on
it. A reviewer channeling reduz bounces on this alone.
*Fig-leaf the proposal may cite:* **QbieShay (2025-03-20, +3):** "I think pass index should be overridable
per-object, if possible." So per-object routing has in-thread demand — but the burden is on the proposal to answer
GPU-driven dispatch, and it does not.

### 1b — "Transparency was declined, so this seam is uncontested" is half-false, and the wrong half hurts

The proposal leans on reduz's "not much point for transparent passes" quote as *its* uncontested seam. In-thread,
that quote was **immediately and heavily contested by senior render engineers**:

> **darksylinc (+28 — the single highest-voted comment):** "**Extreme disagree.** One strong point of compositors
> is that you can fix the limitations of regular alpha blending (even including … OIT). It is also necessary if you
> wish to render special geometry. e.g. in FPS games the gun and hands should be rendered always in front…"
>
> **thygrrr:** "This should most definitely **not be limited to opaque passes only**."

This cuts *against* the proposal, not for it: the people who want transparent compositor work want it **folded into
#7916's unified pass / stencil / custom-buffer model**, not shipped as a parallel primitive with its own render
list and instance flag. The seam is not "declared out of scope and abandoned"; it is "actively contested, with
reviewers pushing to *expand #7916's scope* to cover it." That is the opposite of a safe adjacent niche — it means
the transparent case is *already claimed territory inside the #7916 debate*.

### 1c — csubagio pre-rejected this proposal's exact shape; clayjohn's reflex is "use what exists"

> **csubagio (+18, then +7):** argues *twice* for **fewer** special cases and one composable "pass" primitive, and
> names the failure mode precisely: "flagging of objects/materials/groups for inclusion/exclusion that start
> becoming confusing to name in the UX … 'why is this a material layer flag instead of a pass? Oh, because it's
> transparent?'"

The proposal adds exactly a new `RENDER_LIST_COMPOSITOR_LAYER`, a per-instance `render_layer` flag, and a new
resource — the "compounded future complication" csubagio warned of. And the current maintainer's 2026 reflex for
"get render-pass data to materials":

> **clayjohn (2026-01-23):** "you can already achieve the same thing with a **Texture2DRD** right now… The custom
> buffer approach will make this _easier_ … But the effect can be achieved already without very much code."

That is the #7916-level equivalent of the SubViewport bounce: the bar for *new core surface* is high, and the
default answer is an existing escape hatch. (Genuine gap clayjohn's answer leaves open, per **FuWan722**:
`Texture2DRD` via global uniform breaks with multiple viewports — a real but narrow crack, not a mandate for a new
render list.)

### 1d — One divergence the source *supports* (concede for honesty)

The proposal's **Divergence #2** (identity-`Resource` layers, not #7916's flat 4-slot index) is **vindicated** by
the top comment: **darksylinc (+28):** "4 textures is nowhere near enough… hashed strings are a much more
user-friendly way here (e.g. 'Luminance Texture')." So the identity-over-index divergence is defensible and
in-line with senior feedback. Divergence #1 (per-instance) is the exposed one; Divergence #2 is not.

**Attack #1 verdict.** The honest read is **not** "complementary counterpart they'll welcome." It is *"fold it into
#7916, and answer GPU-driven rendering first."* The proposal's own "Anticipated objection #1" already hedges
toward "landed in #7916's orbit"; the primary-source read hardens that hedge into the **most probable outcome**,
and surfaces a specific unanswered blocker (GPU-driven dispatch) the draft must address or concede.

---

## Attack #4 — Mobile feasibility. VERDICT: **lands hard, verified. Realistically Forward+-only.**

The most concrete, verifiable attack — and it fails for the proposal. A deferred, post-resolve, held-out pass is
**architecturally hostile to Mobile's tile-based subpass model**, not a "mirror."

- Mobile renders opaque + sky + transparent + tonemap as **subpasses of a single render pass / single
  framebuffer** (`render_forward_mobile.cpp:1224-1318`; the subpass tonemap runs *inside* the draw list via
  `draw_list_switch_to_next_pass()` — `renderer_scene_render_rd.cpp:917`), keeping tile-local memory live. A
  deferred pass into a **separate compositor-owned target cannot be a subpass** — subpasses are bound to one
  framebuffer; you must `draw_list_end()` → `draw_list_begin()` on a new target, flushing/resolving the tile.
- Mobile **already knows this**: it **disables the subpass fast-path** the moment a PRE/POST_TRANSPARENT compositor
  effect is present (`render_forward_mobile.cpp:891-903`: `ce_has_pre_transparent` / `ce_has_post_transparent` →
  `using_subpass_post_process = false`). So the held-out primitive forces Mobile off its whole reason for
  existing.
- **Color space differs from Forward+.** On the non-subpass path Mobile runs POST_TRANSPARENT *before* tonemap in
  scene-linear (`render_forward_mobile.cpp:1377-1390`); on the subpass path the tonemap+sRGB convert happen inside
  the chain (`renderer_scene_render_rd.cpp:966`, `convert_to_srgb = !using_hdr`). Forward+ (Clustered), which never
  uses subpasses, resolves MSAA then runs POST_TRANSPARENT on **resolved scene-linear** data
  (`render_forward_clustered.cpp:2436-2459`, tonemap `:2551`). A held-out pass lands on **materially different**
  inputs on the two renderers.

This confirms and hardens the prior review's "Mobile mirrors is hand-wavy" note with line numbers. The proposal's
"capability-gated, both RD renderers" and "Forward+ shown; Mobile mirrors" claims are **not honest as written**:
the realistic scope is **Forward+-only**, or "Mobile only by forcing it off its fast path, in a different color
space." This narrows the renderer-coverage pitch and *reinforces* the "just keep it a fork / wait for #7916" push
(clayjohn has said the team wants compositor to expand *toward* more renderers, incl. eventual Compatibility — an
RD-only, Forward+-realistic holdout primitive swims against that current).

---

## Attack #6 — exported-property timing. VERDICT: **does NOT land. Concede — the proposal is correct.**

No chicken-and-egg. CompositorEffect properties are **live-read every frame**, never cached or "committed":

- Setters push straight to storage when the RID exists (`compositor.cpp:152-159`); getters query storage live
  (`compositor_storage.cpp:142-147`); the renderer polls per-frame via `_compositor_effects_has_flag()`
  (`renderer_scene_render_rd.cpp:257-278`), invoked each frame in fill
  (`render_forward_clustered.cpp:1731-1733`). There is no registration snapshot a late property-set could miss.
- The scenario's compositor is re-read every render (`renderer_scene_cull.cpp:2784`), and NTKey named textures are
  **allocated on-demand on first access** (`render_scene_buffers_rd.cpp:328-351`), queried by identity
  (`get_texture`). An exported `render_layers` read during compositor-effect processing would allocate its keyed
  textures dynamically within the frame, before the callbacks fire.

So `@export var render_layers` fits the existing live-read model cleanly. This attack is **withdrawn**; the
proposal should keep its property-declaration design and can cite this as verified.

---

## Attack #3 — `seed_source = SCENE_COLOR`. VERDICT: **lands (partially). Coherent for the fold, footgun for the other clients.**

Work the example. `SCENE_COLOR` seeds the layer with resolved scene color; held-out geometry draws on top; the
*effect* then composites layer → scene.

- **For a replace/sample composite (the PSX-fold):** correct and useful. The layer *is* the final image
  (scene + held-out geometry), the "composite" is really "replace scene with my distorted layer," no double-count.
  This is the fold's genuine need and the reason `seed_source` was generalized.
- **For an over/additive composite (the decal / NPR-transparency clients the proposal also courts):** a
  **correctness trap**. The scene is baked into the seed *and* composited over the real scene → **double-exposed**.
  Worse, with `SCENE_COLOR` seed and depth-write-off held-out draw, there is **no coverage/alpha channel** telling
  the effect where held-out geometry actually is, so it cannot mask "seed pixels" from "held-out pixels." That
  directly contradicts §3's "v1 ships without aux outputs; coverage is engine-written when needed" — a
  `SCENE_COLOR`-seeded over-composite **needs** coverage in v1.

The deeper point (feeds Attack #7): the fold wants `SCENE_COLOR` + replace; decals want `CLEAR` + over. The seed
and the composite-op are **coupled**, but the interface exposes them as independent free choices, letting a user
pick an incoherent pair and get double-exposure with no warning. **Recommendation:** don't cut `SCENE_COLOR`
(it's the fold's need), but either (a) restrict it to `CLEAR` for v1 and defer `SCENE_COLOR` until coverage
exists, or (b) document the seed↔composite coupling explicitly and ship engine-written coverage *with*
`SCENE_COLOR`, not deferred. As written, it is a footgun disguised as a knob.

---

## Attack #5 — structural-invariant claims. VERDICT: **lands. "depth-write forced off" is a default masquerading as an invariant.**

The proposal asserts "depth-test vs resolved scene depth, depth-**write** forced off" as a *structural invariant*
of a holdout layer. Pressure-tested, it is a *default* that is wrong for a real case:

- **Self-overlapping opaque held-out geometry breaks.** With depth-write off, held-out instances have **no depth
  relationship to each other** — they are pure painter's order (`render_layer_order`, ties by fill order). A far
  triangle drawn *after* a near one in a self-overlapping opaque mesh will **show through** the near one. So the
  layer is correct only for held-out geometry that is mutually non-overlapping or intentionally painter's-ordered
  (a decal quad, a fold plane). For a general opaque held-out object it is visibly wrong. "depth-test vs resolved
  scene depth" advertises depth correctness the layer does not have *within itself*.
- **Multi-layer inter-occlusion is impossible.** A layer that must occlude a *later* layer cannot — nothing writes
  depth, so both test only the frozen scene depth. The handoff's exact objection holds.
- **The conflation.** "Don't write the *scene's* depth" (correct — the scene is resolved) is silently upgraded to
  "don't write *any* depth." A held-out layer *could* own a writable depth attachment seeded from scene depth,
  giving both scene-occlusion **and** correct self-occlusion — a real design choice the "invariant" hides.

So the invariant is really an **untested default** that quietly restricts the primitive to painter's-safe geometry.
It should be stated as a *limitation* ("held-out instances do not depth-occlude one another; single-layer,
painter's-ordered geometry only") — or the layer needs an optional own-depth mode — not sold as a defining
invariant. (The other two invariants — `view_count` matches scene, post-resolve ⇒ single-sample — are genuinely
structural and survive.)

---

## Attack #2 — one client for permanent hot-path surface. VERDICT: **lands. Residual demand ≈ the fold.**

After the demand demotion (`adversarial-review-subviewport.md` Q2), what *uniquely* needs the B∧C∧D intersection
is the fold, plus the small honest ordering asks (#3986 ≈26, #11251 ≈17 — and #11251 wants order-*independence*).
The 2025–26 #7916 comments add real transparency-adjacent pain (FuWan722's clouds, OhiraKyou's decal buffers) —
but every one of those is a **`CUSTOM_BUFFER` / pass-inside-#7916** ask, not a held-out-list-in-caller-order ask;
clayjohn routes them to `Texture2DRD` or #7916, not to a new primitive. So a **permanent new render list +
fill-branch + per-instance ABI in the two most-used RD renderers** is justified by essentially **one client
archetype**. That is thin. The honest options are (a) **keep it a fork** (ADR-0001), or (b) **contribute the
*decomposed* pieces** — a caller-order transparent sort (#3986) and a resolved-depth-share — per the SubViewport
review's split recommendation, at a lower review bar, rather than a standalone new-render-list primitive.

---

## Attack #7 — "generalization by subtraction." VERDICT: **partially lands. API subtracted; engine complexity relocated.**

Real: the *interface* did get smaller and honester (7-field target → 2-field identity token; order key moved to the
instance; NaN-cliff gone). But "subtraction" overclaims on the engine side:

- The **fill-branch still touches the hottest loop** (`_fill_render_list`) and the held-out instances "must be
  gathered during the main fill" (the proposal's own stated coupling) — so the primitive's *geometry* is collected
  in the hot path even though its *pixels* are deferred. Hot-path surface did **not** shrink.
- The **invariants relocated decisions, didn't remove them**: depth-write-off (Attack #5) and seed↔composite
  coupling (Attack #3) are real choices pushed out of the API and into engine behavior, where they will resurface
  as bug reports / feature requests ("why does my held-out object self-clip?", "why is my decal double-exposed?").
- Coverage is deferred but **`SCENE_COLOR` needs it** (Attack #3) — a subtraction that isn't quite valid.

So: subtraction on the API façade, **relocation** underneath. Honest, but the proposal should not claim the engine
touch got smaller — it got *moved and hidden*.

---

## Consolidated verdict & recommendation

| # | Attack | Verdict |
|---|---|---|
| 1 | #7916 sequencing / GPU-driven collision | **Lands** — fold into #7916; answer GPU-driven dispatch |
| 2 | One client for permanent surface | **Lands** — fork or decompose |
| 3 | `SCENE_COLOR` seed | **Partial** — fold-only-coherent footgun; restrict or add coverage |
| 4 | Mobile feasibility | **Lands hard (verified)** — realistically Forward+-only |
| 5 | depth-write-off "invariant" | **Lands** — a default; breaks self-overlap / multi-layer |
| 6 | property lifecycle timing | **Does not land** — concede; proposal correct |
| 7 | generalization by subtraction | **Partial** — API subtracted, engine complexity relocated |

**The capability (B∧C∧D) is still real and SubViewport-unreachable** — that verdict from the prior review stands.
What this review kills is the *standalone packaging and the "both RD renderers, welcomed counterpart" framing*:

1. **Reposition from "standalone counterpart" to "wait-for / land-inside #7916."** The proposal's own hedge
   becomes the headline: this belongs in #7916's orbit, and the honest first step is to ask the #7916 owners
   (clayjohn / reduz / BastiaanOlij), not to open a competing design.
2. **Answer GPU-driven rendering or concede the per-instance divergence.** This is the specific unaddressed blocker
   the primary source surfaced. Either argue why per-instance holdout is compatible (it routes *before* material
   dispatch, so it may be — but say so), or move the opt-in back toward a material/`render_mode` directive and lose
   the "any StandardMaterial3D unchanged" selling point.
3. **Downgrade the renderer claim to Forward+-realistic.** Stop saying "Mobile mirrors"; state the subpass-chain
   incompatibility.
4. **Fix `seed_source`:** restrict `SCENE_COLOR` or ship coverage with it; document the seed↔composite coupling.
5. **Restate depth-write-off as a limitation, not an invariant** (or add an optional own-depth mode).
6. **Keep the property design** (Attack #6 verified) and **the identity-over-index divergence** (darksylinc +28).
7. **If the team still declines the seam, the fork remains the correct fallback (ADR-0001)** — but decomposing
   into (caller-order sort) + (resolved-depth-share) is the lower-bar upstream path if any of it is to land.

### One-line honest pitch (revised post-#7916)

> *The B∧C∧D held-out layer is a real engine-only capability — but #7916 is unshipped, its author chose
> material-based dispatch for GPU-driven-rendering reasons this proposal's per-instance opt-in contradicts, its own
> reviewers want the transparent case folded in rather than forked out, and the pass can't ride Mobile's subpass
> chain. So the honest move is to bring B∧C∧D to the #7916 owners as the transparent case they're already arguing
> about — not to file a standalone primitive that reads as a competing fork.*
