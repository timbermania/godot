# Engine implementation plan for the general material-side primitive

Type: grilling
Status: closed
Blocked by: 05
Assignee: Aaron Curry (claimed 2026-07-31)

## Question

Given the settled proposal (`docs/proposal-draft-compositor-render-layer.md`: a #7916 render **pass**
+ two extensions — a **caller-order key within a pass**, and a **held-out target** the effect
seeds/composites in-frame) and the near-complete reference branch's diff (ticket 05), design the clean,
upstream-PR-shaped C++ implementation.

**Pivotal decision (surfaced by ticket 01/05):** `spike/compositor-consume-material-output` is a
near-complete `compositor_fold` implementation, but **ticket 05 recommends reimplement-clean on
`feature/render-to-compositor`, NOT clean-up-in-place** — the spike's membership sits on the opposite
axis from the settled proposal, so in-place cleanup is mostly subtraction. Port only the two reusable
pieces (the unit-tested `fold_order_sort` seam; the seed→held-out-pass→composite frame shape); keep the
spike as the on-GPU-verified **oracle**. Confirm or overturn that recommendation first, then:

Resolve:

1. **Membership seam — THE CRUX RISK.** ⚠️ The spike expresses membership as a `render_mode
   compositor_fold`, i.e. a **shader-permutation axis** — which ticket 05 flags is *exactly* what the
   proposal says splits reduz's GPU-driven indirect-draw batch. "Material-side membership" (settled) must
   therefore be expressed **batch-safely** — a material property/flag that groups instances **without**
   forcing a distinct shader variant, not a `render_mode`. Resolve the precise batch-safe expression and
   confirm it against `docs/research-gpu-driven-and-unification.md`. (This does NOT reopen the settled
   material-vs-per-instance decision; it pins *how* material-side is encoded so it doesn't split the batch.)
2. **Caller-order key** — port `fold_order_sort.h` (keys on `owner->sorting_offset`, uncapped float,
   NaN→+INF, stable submission tie-break, already unit-tested). Confirm `sorting_offset` (not the design
   doc's older `render_priority` ±127) as the order key. Keep the **pure order comparator** as a test-first
   unit under `tests/servers/rendering/` (proposal §Tests).
3. **Held-out target** — the spike's target is compositor-owned via magic-string scope
   `RB_SCOPE_COMPOSITOR_FOLD` (userland seeds Pass A, engine folds in LOAD/depth-test-no-write after
   resolve/before POST_TRANSPARENT, userland composites Pass C). The frame *shape* matches the proposal;
   decide the clean **ownership + naming** (typed resource vs magic string) for the RenderingServer surface.
4. **Seams & scope** — what is the minimum clean engine surface that makes the fold work AND reads as
   a credible upstream PR. Which of the fork's hacks get promoted to real API vs dropped.

Use `codebase-design` / `Plan`. Output an implementation plan (the C++ build graduates from the fog
as task tickets after this resolves).

## Answer

**Resolved 2026-07-31.** Implementation plan written to
`.scratch/compositor-spike/plans/02-engine-implementation-plan.md` — file:line-grounded against both the
empty `feature/render-to-compositor` tree and the spike oracle `~/Repos/godot @
spike/compositor-consume-material-output`.

**Two decisions (grilled):**
1. **Membership = a general `render_mode compositor_layer`** (shader-permutation), NOT the use-case name
   `compositor_fold`. This is the *only* batch-safe reading of the settled "material-side" membership:
   reduz's GPU-driven cull sorts *by shader type*, so batch-safety-by-construction comes precisely from
   being a shader variant. The ticket's original item-#1 ask ("material-side, batch-safe, *not* a
   render_mode") was internally incoherent against `research-gpu-driven-and-unification.md` and is retired.
   Cost accepted: a `StandardMaterial3D` cannot join without a `ShaderMaterial`.
2. **Target identity = the member material carries `render_layer : CompositorRenderLayer`**; engine owns
   allocation keyed by that resource's identity (NTKey), N layers coexist, effect reads via
   `get_layer_texture(resource)`. The magic-string scope is dropped. A binary render_mode can't carry
   identity, so identity rides a material resource param — preserving the proposal §1 public API.

**Approach:** reimplement clean, porting only the pure caller-order comparator and the
seed→held-out-pass→composite frame shape from the spike; keep the spike as the GPU-verified oracle.

**Confirmed defaults (flagged for veto):**
- Order key = exact `int32_t render_layer_order` (not the spike's uncapped float `sorting_offset`).
- v1 seeds = `CLEAR` + bound `Texture`; **`SCENE_COLOR` deferred** (needs an engine-written coverage
  attachment). The FFT fold authors its own display-space seed in userland Pass A → bound-`Texture` covers
  the proof. **Ticket 03 must validate this seed mapping; if the fold genuinely needs engine `SCENE_COLOR`,
  that is a ticket-03 finding that reopens this.**
- Multi-layer via one render list + pass-time partition by NTKey (enum is fixed-size); single-fold proof =
  one-partition fast path.

**The C++ build graduates into 7 task tickets (Steps 1–7):**
- [Step 1 — pure caller-order comparator, test-first](06-build-order-comparator.md) (no deps)
- [Step 2 — register `compositor_layer` render_mode + Mobile gate](07-build-render-mode-registration.md) (no deps)
- [Step 3 — `CompositorRenderLayer` resource + engine-owned allocation](08-build-render-layer-resource.md) (no deps)
- [Step 4 — per-instance props + fill routing + new render list](09-build-instance-props-fill-routing.md) (blocked by 06,07,08)
- [Step 5 — the held-out pass](10-build-heldout-pass.md) (blocked by 09)
- [Step 6 — strip FFT policy; guard-rails→invariants](11-build-strip-fft-policy.md) (blocked by 10)
- [Step 7 — public accessor + effect property + docs](12-build-public-surface-docs.md) (blocked by 10)

Build frontier = Steps 1/2/3 (parallelizable).

**Open questions carried into the build (non-blocking):** order-key transport channel; resource-identity→
NTKey keying/lifetime; partition storage (compound-key secondary sort preferred); `stage`/MSAA earlier-path
deferral; Mobile hard-fail vs warn. Detail in the plan file's "Open questions" section.
