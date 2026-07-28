# Evaluation: `compositor_fold` through a Godot upstream-maintainer lens

**Written:** 2026-07-28 · **Owner:** Aaron Curry · **Engine tree:** Godot 4.8-dev
**Branch:** `spike/compositor-consume-material-output` · **Scope:** the engine feature only (the FFT game is context)

**This is an assessment, not a plan to merge.** It pressure-tests the feature as if it were an incoming PR to
`godotengine/godot`, reviewed by the rendering team (Clay John / Bastiaan Olij / the `render_mode` +
CompositorEffect owners). It supersedes nothing; it judges the design in
[`engine-shaded-display-fold.md`](engine-shaded-display-fold.md) §10 plus this session's four working-tree
robustness additions.

The verdict up front so the rest can be read as support for it:

> **Bounce as a core PR. Viable as a rendering-team proposal — but only after reframing from a use-case
> (`compositor_fold`) to the general primitive it actually is: _render flagged materials, through their real
> material pipeline, in caller order, into a compositor-owned auxiliary target._** The capability is real and
> not otherwise achievable (it dissolves a genuine `CompositorEffect` wall). The *packaging* — a use-case-named
> render_mode, a stringly-typed texture handshake, a silent blend override, and a one-off public capability
> method — is what a maintainer bounces. The good news: the general primitive is _tighter_ than the specific
> one, not looser. The work is subtraction, not addition.

---

## 0. The feature in one breath (so the eval isn't archaeology)

A spatial material declaring `render_mode compositor_fold` has its transparent surfaces pulled out of
`RENDER_LIST_ALPHA` into a new `RENDER_LIST_COMPOSITOR_FOLD`, which the Forward+ renderer:

1. sorts by each instance's **`sorting_offset`** (caller order, **no camera-depth term**);
2. draws **after** the transparent resolve, **before** the `POST_TRANSPARENT` compositor callback;
3. into a **compositor-owned** named texture `("compositor_fold","color")` (asserted `A2B10G10R10_UNORM`),
   **LOAD** not clear, so a `CompositorEffect`'s Pass-A seed survives;
4. through the material's **real forward pipeline** (this is the whole point — no GLSL re-port), except the
   **alpha** blend is force-overridden to `ADD/ONE/ONE` as a coverage mask;
5. behind `WARN_PRINT_ONCE` guard-rails (non-Linear tonemapper / upscaling / MSAA / missing scratch / wrong
   format).

Plus, this session: Mobile binds the flag only to a `WARN` (no fold pass exists there), and
`RenderingServer.is_compositor_fold_supported()` is a new bound method returning
`get_current_rendering_method() == "forward_plus"`.

Total surface: **7 files, ~150 lines.** Small enough to hold in your head — which is exactly why the review is
about *shape*, not *size*.

---

## 1. What would a maintainer look for? (Q1)

Godot's rendering team has a well-known posture, and this feature trips several of its tripwires. Checked
against what actually gets rendering changes merged:

| Maintainer concern | This feature | Verdict |
|---|---|---|
| **Generic, curated `render_mode` vocabulary** | `compositor_fold` names a *consumer/use-case*. All 24 existing spatial render_modes name a *capability* (`unshaded`, `depth_draw`, `cull`, `alpha_to_coverage`…). | 🔴 Off-pattern — the single loudest tell |
| **No stringly-typed engine↔userland contracts** | The `("compositor_fold","color")` named-texture handshake is a magic string with no compile-time check, shared across the engine/renderer seam and a userland `.gd`. | 🔴 Discoverability trap |
| **No silent behavior changes** | The coverage-α override silently rewrites a material's *declared* alpha blend. `blend_sub` no longer subtracts alpha. | 🟡 Surprising; needs to be opt-in or documented-loud |
| **Fail safe, not fail corrupt** | Color-corrupting preconditions (tonemapper/size/MSAA/format) `WARN_PRINT_ONCE` **and continue**. Only *missing scratch* actually skips. | 🟡 Warn-and-corrupt < warn-and-skip |
| **ABI / compat burden they carry forever** | New `RENDER_LIST_*` enum, new per-list instance buffer, new hot-path pass in the *most-used* renderer, a new reserved shader word, a new bound `RenderingServer` method. | 🟡 Bounded but permanent |
| **Cross-renderer parity or an explicit gate** | Forward+ only; Mobile warns; Compat/GLES3 can't (RD-only). | 🟡 Needs to be a *documented* gate, not an ad-hoc method |
| **A real use-case not achievable with existing tools** | Yes — a `CompositorEffect` cannot invoke a `ShaderMaterial`; the feasibility doc argues GDExtension can't bridge it either. | 🟢 The strongest merge argument |
| **Docs (class-ref for the render_mode)** | None. | 🔴 Non-starter as-is |
| **Tests / minimal repro** | None in-tree (the spike harness lives in `/tmp`, not the test suite). | 🔴 Non-starter as-is |
| **Perf in the hot path** | Two per-frame `LocalVector` allocs in the stable sort; one extra pass + uniform-set setup per frame when the list is non-empty. | 🟢 Minor; flag it, hoist the allocs |

Two 🔴s are fatal to a *core PR* on their own (docs, tests), and two more (use-case name, magic-string
contract) are the ones that trigger a "this needs a proposal first" response. None are fatal to the *idea*.

---

## 2. The general capability hiding inside (Q2)

Strip the words "fold", "PSX", "display-space", "combat" and read what the engine actually does:

> **Draw flagged materials — in a caller-specified order, through their real material pipeline — into an
> auxiliary color attachment that a `CompositorEffect` owns and names.**

That is a **render-to-named-compositor-target-with-explicit-ordering** primitive. It exists to dissolve one
specific, real wall (call it R1, from the design docs):

> A `CompositorEffect` runs raw `RenderingDevice` compute/raster. It **cannot invoke a `ShaderMaterial`.** So
> anything a compositor wants to composite that is *shaded by a Godot material* must today be hand-ported to
> GLSL and kept byte-synced forever (in FFT: a 939-line `unit_sprite_body.gdshaderinc` plus nested PSX
> includes). The engine is the only thing that *can* shade a material; the compositor is the only thing that
> owns the auxiliary target. This feature is the bridge between them.

That wall is not FFT-specific. The same bridge serves, off the top:

- **Decals / stamps** shaded by a real material into a compositor G-buffer aux target.
- **Stylized / NPR ordered transparency** that needs hand-ordered, not depth-sorted, compositing.
- **Selective ID / mask / outline buffers** where the mask must be produced by the object's *own* material.
- **Impostor / portal capture** of real-material objects into a named texture a later pass consumes.
- **Custom OIT experiments** — the exact class of thing the design docs walled off (SSBO/A-buffer) because
  they needed per-pixel storage; this needs none.

**Reframe the whole feature around this primitive and PSX-fold becomes one client of it.** That reframing is
also the single most important thing to do before showing this to upstream: it converts "here's my game's
rendering trick" into "here's a missing engine capability, and here's my game as the motivating client." The
first gets bounced; the second gets a proposal thread.

---

## 3. Can it be made more generalizable without a god-feature? (Q3)

This is the heart of the evaluation, and it's a **deep-module seam** question, so I'll use that vocabulary
precisely.

### 3.1 The current interface is *shallow* — and that's the real problem

The flag *looks* like the smallest possible interface: one word, `compositor_fold`. But **the interface is
everything a caller must know to use the module correctly**, and here that is:

1. add `render_mode compositor_fold` to the material;
2. stamp each instance's `sorting_offset` with your fold-order key (an existing channel, repurposed);
3. author a `CompositorEffect` that allocates a texture named **exactly** `("compositor_fold","color")`, at
   **`internal_size`**, format **`A2B10G10R10_UNORM_PACK32`**, usage including **`CAN_COPY_TO`**, seeds it at
   **`PRE_TRANSPARENT`**, and resolves it at **`POST_TRANSPARENT`**;
4. guarantee **Linear** tonemap, **no MSAA**, **native resolution**;
5. know that the material's declared **alpha** blend is silently replaced by an `ADD/ONE/ONE` coverage mask.

That is a **large, mostly-implicit, stringly-typed, invariant-laden interface** hiding behind a one-word flag.
By the deep-module test this is a **shallow module wearing a deep module's clothing**: the visible surface is
tiny, but the true surface a caller must learn is sprawling and undocumented. The `WARN_PRINT_ONCE` guard-rails
are the tell — they exist precisely because so much of the real interface is unstated and unenforceable at
author time.

**The deletion test** cuts the other way, though, and this is what saves the idea: delete the feature and the
complexity doesn't vanish — it *reappears* as 939 lines of hand-ported GLSL in every consumer that wants to
composite a real material. So the module is *earning its keep*; it's just packaged as the wrong shape. The job
is to **re-seam**, not to delete.

### 3.2 The feature welds five separable decisions

| # | Decision | Currently | Separable? |
|---|---|---|---|
| a | **Route** flagged material to an alternate target/list | hardcoded to one list/target | yes — this is the load-bearing primitive |
| b | **Order** that list by a caller key, not depth | `sorting_offset`, uncapped float | yes — reuses an existing channel |
| c | **Coverage-α** blend override | always on when flag set | yes — orthogonal to routing |
| d | **Target identity + format** | hardcoded string + `A2B10G10R10_UNORM` | yes — should be compositor-declared |
| e | **Pass slot** (after transparent / before `POST_TRANSPARENT`) | hardcoded | mostly fixed by the seam, but should be *stated* |

A maintainer's instinct is "make a–e orthogonal knobs." **Resist that** — five independent knobs is the
*leaky-god-feature* failure mode, the opposite error. Each knob added to the *interface* makes the module
shallower. The design skill is to find the seam that keeps a–e mostly *inside* the implementation while
exposing the minimum a caller genuinely must vary.

### 3.3 The seam I'd actually propose

Keep **one deep primitive**, expose **two things**, and make **one contract explicit**:

- **One render_mode, renamed to the mechanism**, e.g. `render_mode compositor_pass` (or
  `render_to_compositor` — bikeshed later). It means exactly "route this material's shaded transparent output
  into a compositor-owned target instead of the screen." That is decision (a), and *only* (a). This name would
  sit comfortably in the generic list next to `unshaded` and `alpha_to_coverage`.

- **Ordering: keep `sorting_offset`.** It's already public, already per-instance, already uncapped. Overloading
  it is defensible (see §5.D) and costs zero new API. Document that in the fold list it *is* the order key.

- **Make the target contract a real API, not a magic string.** The `CompositorEffect` should *declare* the
  named target it owns (name + format + size policy) through a typed method, and the render_mode should bind to
  *that* declaration — so the handshake is discoverable, checkable, and reusable by the decal/mask/NPR clients
  above. This is decision (d) lifted out of a hardcoded string into the compositor's own interface, where it
  belongs. It also generalizes for free: a second client just declares a second target.

- **Coverage-α (c) stops overloading `color.a` entirely** — give it its own dedicated aux channel rather than
  silently rewriting the material's declared alpha blend (a reviewer will not accept "your `blend_sub` quietly
  stopped subtracting alpha"). See **§3.5** for the full mechanics and why a dedicated channel — not a smarter
  or opt-in override — is the right generalization (it also unlocks the linear/HDR and WBOIT-class uses).

- **Pass slot (e) stays hardcoded** but *documented* as part of the contract ("folds during the transparent
  pass, after resolve, before `POST_TRANSPARENT`"). It's fixed by the seam; exposing it as a knob would be
  gratuitous depth-loss.

Net effect on the interface: the caller still writes one render_mode + sets `sorting_offset` + writes a
compositor pass — but now the render_mode *names what it does*, the target handshake is *typed and
discoverable*, and the alpha override is *not a surprise*. Same behavior, genuinely deeper module: more
capability (decals, masks, NPR, OIT) reachable through a *smaller and more honest* interface. This is
generalization-by-subtraction — the good kind.

### 3.4 What I would *not* do

- **Don't** expose the target format, usage flags, or pass slot as render_mode arguments. That's five shallow
  knobs and a combinatorial pipeline-variant explosion.
- **Don't** invent a dedicated ordering channel (a new `fold_order` per-instance field) unless the
  `sorting_offset` precision cliff (§5.D) actually bites a real consumer. One adapter (FFT) is a *hypothetical*
  need for a new channel; wait for a second.
- **Don't** try to make the coverage/discard resolve generic. That lives in Pass C, in userland, correctly.
  The engine's job stops at "draw into the named target."

### 3.5 The alpha channel and color-space are the *target's* business, not the fold's

Two questions surfaced in review — "what is the alpha actually doing, exactly?" and "should this be flexible
enough to post-process in gamma *or* linear?" — turn out to have the same answer, and it sharpens the seam
above. Grounded in `renderer_rd/storage_rd/material_storage.cpp:651` (`blend_mode_to_blend_attachment`).

**Exactly how the alpha works.** Vulkan blends color and alpha with *fully independent* ops and factors — one
attachment carries two separate equations. The feature exploits that split, so alpha plays **two unrelated
roles** in one pass:

- **Role A — the fragment's alpha as a per-prim *weight* (input, color math).** Every mode uses
  `src_color_blend_factor = SRC_ALPHA`, so the fragment's alpha scales *how much color it deposits* — a
  strength knob, not transparency. The "25% PSX" mode is just `blend_add` with the front pre-scaled (α or
  color), not a distinct engine mode.
- **Role B — the attachment's alpha as an accumulated *coverage* channel (output).** This is what the
  `compositor_fold` override rewrites. Stored-alpha per mode, stock vs. override (`alpha_op=ADD, src=ONE,
  dst=ONE`):

  | Mode | Stock stored alpha | With override |
  |---|---|---|
  | `add` | `dst.a + src.a²` (SRC_ALPHA factor squares it) | `dst.a + src.a` |
  | `sub` | `dst.a − src.a²` (**REVERSE_SUBTRACT** — drives coverage *down*) | `dst.a + src.a` |
  | `mix` | `src.a + dst.a·(1−src.a)` | `dst.a + src.a` |

  The override collapses all three to **`α = Σ src.a` (UNORM-saturating)** — a monotone coverage accumulator
  *independent of the color op*. That's the point: a `blend_sub` prim subtracts color while its coverage still
  *adds*, so a sub-heavy pixel stays "touched" instead of falling to α≤0 and being falsely discarded by Pass C.
  The color side is untouched, so `mix` still non-commutes (why the CPU sort keeps mixes solo) — the override
  unifies *coverage*, not *color*.

**Color-space is already flexible — the engine is agnostic.** The hardware blender computes `dst OP src` on raw
numbers; it has no notion of linear vs gamma (design doc §3). A fold is gamma-space *iff both operands hold
gamma numbers*, which is determined entirely by (a) what the compositor **seeds** and (b) what the materials
**emit** — neither of which the engine controls. So a **linear** fold already works through the identical pass:
seed linear, emit linear, blends compose in linear. Display-space PSX is the clever *exception* (both seed and
`unshaded`/no-`source_color` texels happen to be display-encoded), not the rule. What *blocks* a linear fold
today is not the fold logic but the hardcoded `A2B10G10R10_UNORM` assumption — UNORM clips `[0,1]` at ~10 bits
and gives alpha only **2 bits (4 levels)**. Linear-HDR wants `RGBA16F`. → the same §3.3 conclusion: **let the
compositor declare the target format**; gamma-vs-linear becomes a property of the declared target, not the pass.

**"Could we store arbitrary shader parameters instead of hijacking alpha?"** The word "parameter" hides a
discriminator that decides the whole answer:

- A **per-draw constant** (`shader_parameter`, instance uniform, `sorting_offset`) is one value per *draw* — it
  **cannot accumulate across overlapping prims**, so it can *never* replace the alpha, whose entire job is a
  per-pixel reduction ("how much touched *this* pixel"). Only the blender (or explicit compute) produces that.
- A **per-pixel accumulated value** *must* be a blendable **attachment channel**. So "arbitrary outputs" really
  means **MRT**: the fold binds more attachments, the fragment writes them, each gets its own format + blend
  op. That is the genuinely clean generalization — coverage leaves color's 2-bit alpha for its own
  `R8`/`R16F`; a WBOIT weight gets `R16F`; an ID gets `R32UI`; `color.a` means alpha again — and it **still
  rides fixed-function blend** (zero compute, the property that makes this feature mergeable at all).

  The cost is the shader *language*, not the pipeline: Godot spatial shaders have a **fixed fragment-output
  interface** (`ALBEDO`, `ALPHA`, `EMISSION`…). The color pass already uses MRT internally (color / separate-
  specular / motion — the `{ blend_attachment, Attachment(), Attachment() }` in the diff), but those slots are
  **engine-defined**; there is no user-facing arbitrary fragment `out` (`CUSTOM0..3` are vertex *inputs*).
  Exposing truly arbitrary outputs is a shader-language + compiler change that dwarfs this feature, plus a
  pipeline-variant explosion and a fatter target handshake. Upstream would decouple it.

**The tight shape is enumerated, not arbitrary.** Arbitrary outputs are an unbounded interface (every material
invents its own contract; the compositor can't know what to allocate). The deep version is a **small enumerated
aux-output vocabulary** the shader opts into, each mapping to a compositor-declared channel — e.g. `COVERAGE →
R8/add`, `WEIGHT → R16F/add` (WBOIT), `ID → R32UI/max`. Bounded (compositor knows what to allocate), typed (no
stringly-typed trap), each entry a reusable capability. **Ship v1 as just one step of it:** split coverage out
of `color.a` into a single dedicated aux attachment — the engine writes it from the fragment alpha, so *no
shader-language change is needed yet*, alpha is freed for real transparency, coverage gets full precision, and
it's the exact wiring WBOIT reuses. The fully-arbitrary version is the same idea with the language change bolted
on later, when a second consumer justifies it (one adapter = hypothetical seam).

**This supersedes the "make the alpha override opt-in" line in §3.3.** The better move isn't a smarter override
on `color.a` — it's to **stop overloading `color.a` at all** and give coverage/weight their own channel. Same
"generalize by subtraction" thesis: the PSX-specific alpha policy dissolves into a right-sized, declared channel
that also unlocks the linear/HDR and WBOIT-class uses.

---

## 4. Is it tight enough? (Q4)

Tightness audit, harshest reading:

- 🔴 **Stringly-typed contract.** `("compositor_fold","color")` is duplicated across the engine and a userland
  `.gd` with no shared symbol. Rename one side and it silently no-ops (the engine warns "no scratch", skips,
  and the screen just… doesn't fold). Fix: §3.3's typed target declaration.
- 🟡 **Silent alpha override.** Correct for the FFT coverage mask, genuinely surprising as a general rule —
  a material author reading their own shader has no way to know `color.a` was rewritten, and UNORM alpha is
  only 2 bits so it can never be more than a binary "touched" flag. Fix (see **§3.5**): move coverage to its
  own dedicated aux channel instead of overloading `color.a` — right-sized, non-surprising, and the same wiring
  a WBOIT weight or an ID channel would reuse.
- 🟡 **Warn-and-corrupt guard-rails.** Non-Linear tonemap / upscaling / MSAA / wrong-format all `WARN` and then
  **fold anyway**, producing corrupted color. A maintainer's likely ask: on a *color-correctness* precondition
  violation, **disable the fold** (skip the pass) rather than warn-and-corrupt. Warn-and-skip is defensible;
  warn-and-corrupt reads as "silent corruption with a log line nobody sees." (Contrast: the *missing-scratch*
  path already skips correctly — apply that same posture to the format/size guards at least.)
- 🟡 **`is_compositor_fold_supported()` is one-off public API.** A use-case-named boolean method on
  `RenderingServer`, bound to script, carried forever. If the render_mode is renamed to a generic primitive,
  the *capability query should be generic too* (or derivable from the primitive's existence). Adding permanent
  public API named after one game's feature is exactly what upstream trims.
- 🟢 **Coverage-α *decoupling itself* is clean.** Forcing alpha `ADD/ONE/ONE` while leaving color add/sub/mix
  untouched is a legal independent-blend state and is the right mechanism (avoids a stencil pass). The critique
  is that it's *implicit*, not that it's *wrong*.
- 🟢 **Stateless RID handoff is the right instinct.** The engine allocates/clears nothing and folds into the
  RID it's handed (LOAD, preserving the seed). That's a genuinely deep, tight contract — the buffer lifecycle
  stays out of the engine patch. Keep this exactly.
- 🟢 **No-depth-sort + `depth_draw_never` + depth-test-against-opaque** is correct and side-effect-free; the
  `sorting_offset` repurposing doesn't perturb anything because the list never depth-sorts.
- 🟡 **Per-frame allocs.** The stable sort allocates two `LocalVector`s every frame. Correct (introsort is
  unstable and the consumer *does* produce ties — bare integer buckets on callback prims), but hoist to reused
  members before anyone benchmarks it.

The stable-sort fix (this session) is worth calling out as *correctly motivated*: equal `sorting_offset` prims
must fold in submission order, and `SortArray` is introsort (unstable). Sorting an index permutation with an
`(offset, index)` key is the right fix. It's the *allocation*, not the *logic*, that needs a follow-up.

---

## 5. What matters to *them* — merge-or-bounce, and the seed threads (Q5 + §3.A–G)

Walking the handoff's seed threads and landing each:

- **A. Does it belong in core, at what altitude?** Yes in core (not an addon) — because R1 is a hard engine
  wall (only the engine can shade a material into a compositor target) and the feasibility doc already
  establishes GDExtension can't bridge it. But the *altitude* is wrong: it's pitched as a feature, it should be
  pitched as a **primitive**. Core-worthy capability, non-core-worthy packaging.
- **B. The real general capability** — nailed in §2. This is the framing that gets a proposal read.
- **C. Naming & surface** — `compositor_fold` is the one use-case-named render_mode among 24 capability-named
  ones (grep-confirmed). Rename to the mechanism (§3.3). This is non-negotiable for upstream.
- **D. `sorting_offset` repurposing** — *acceptable overload, with eyes open.* It's clever and zero-API, and
  the fold list never uses it for its native depth-perturbation purpose, so there's no semantic collision *at
  the engine*. The risk is the **precision cliff the real consumer already hit**: packing `bucket + fold_idx·1e-4`
  approaches float32 ULP at high bucket counts. That's a *consumer* packing problem, not an engine one — the
  engine just sorts a float. So: keep `sorting_offset`, but **document the overload**, and treat a dedicated
  ordering channel as a *later* addition justified by a *second* consumer, not now (one adapter = hypothetical
  seam).
- **E. Cross-renderer parity** — Compositor is RD-only, so Compat/GLES3 are out by construction. Forward+ vs
  Mobile is the real axis. Mobile blends transparents in linear (`÷ luminance_multiplier`), which corrupts raw
  display texels, so implementing there is *wrong*, not just absent. The right upstream posture is a
  **documented capability gate**, which the feature has in spirit (`is_compositor_fold_supported()` + Mobile
  `WARN`) but expressed as one-off API. Fold the gate into the generic primitive's story.
- **F. Guard-rail severity** — see §4. The color-correctness ones should **disable**, not warn-and-corrupt.
  The stringly-typed contract is the discoverability trap. The alpha override is the surprising-mutation trap.
- **G. What gets it merged** — minimal orthogonal *well-named* API (needs the §3.3 rename + typed target),
  no silent corruption (needs the guard-rail severity change + opt-in alpha), a real use-case with a demo/test
  (needs an in-tree minimal repro), and docs. The ABI burden (new render_list + pass + reserved word) is
  real but bounded and comparable to other accepted render_modes.

---

## 6. Recommendation

**Path: rendering-team proposal first (`godotengine/godot-proposals`), not a drive-by core PR.** This is a new
cross-cutting seam between the material pipeline and `CompositorEffect`; that class of change goes through a
proposal in Godot, always. The current branch is an *excellent working spike to attach to that proposal* — it
proves the color-space-agnostic-blender premise on real hardware and proves the R1 wall dissolves. It is not a
merge candidate in its current packaging.

What each path demands, concretely:

| Path | What it takes |
|---|---|
| **Proposal-first (recommended)** | Reframe title/abstract around the *primitive* (§2). Present PSX-fold as the motivating client, not the feature. Bring the spike + the raw-value/alpha/mix probe evidence. Ask the rendering team the seam question (§3.3) directly — they own the vocabulary and the pass-slot decision. |
| **Core PR (if the proposal lands)** | Rename `compositor_fold` → generic mechanism. Replace the magic-string handshake with a typed compositor-target declaration. Make coverage-α opt-in. Flip color-correctness guard-rails from warn-and-corrupt to disable. Drop or genericize `is_compositor_fold_supported()`. Add class-ref docs for the render_mode + an in-tree minimal repro test. Hoist the sort allocs. |
| **Module / fork (fallback)** | Keep exactly as-is. Legitimate if upstream declines the seam — but the feasibility doc's "GDExtension can't do this" argument is *ammunition for core*, so lead with the proposal before settling here. |

**The one-sentence pitch to the rendering team:** *"A `CompositorEffect` can't invoke a `ShaderMaterial`;
here's a render_mode that lets a material shade itself into a compositor-owned named target, in caller order —
turning ~940 lines of hand-ported-and-forever-synced GLSL into a `render_mode` line. PSX display-fold is the
first client; decals, ID/mask buffers, and NPR ordered transparency are next."*

That framing is true, it's general, and it's tighter than what's on the branch today — which is the whole
finding: **the feature is real; the packaging is a use-case where a primitive belongs.**

---

## 7. Appendix — items explicitly *out* of scope for a first proposal

Park these so the proposal stays minimal (each is a "later, if a second consumer needs it"):

- A dedicated per-instance ordering channel (keep `sorting_offset` until the precision cliff bites twice).
- Mobile implementation (it's *wrong* there, not just missing — document the gate instead).
- Multiple simultaneous named targets / MRT fold (the typed-target API enables it, but v1 needs one).
- **Arbitrary shader-defined aux outputs** (§3.5) — needs a spatial-shader fragment-output *language* change;
  much larger than this feature, decouple it. The bounded v1 is just "coverage gets its own aux channel"
  (engine-written, no language change); a small *enumerated* aux-output vocab (`COVERAGE`/`WEIGHT`/`ID`) comes
  later when a second consumer justifies it.
- Generic coverage/discard in the engine (stays in userland Pass C — correctly).
- Exposing pass-slot / format / usage as render_mode arguments (shallow knobs; keep inside the implementation).
