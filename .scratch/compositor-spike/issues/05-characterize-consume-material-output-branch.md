# Characterize the actual reference branch: spike/compositor-consume-material-output

Type: research
Status: resolved
Blocked by: —

## Question

Ticket 01 revealed the branch the FFT game actually targets is **`~/Repos/godot` @
`spike/compositor-consume-material-output`** (not the ancestor `spike/forward-plus-display-space-additive`).
It is a near-complete `compositor_fold` implementation with its own ADR-0001 + design docs. The engine
implementation plan (ticket 02) needs its diff characterized in depth. Work READ-ONLY.

Produce, with file:line refs and key diff hunks vs base `aac1c92f5f`:

1. **`compositor_fold` render_mode & material-side membership** — where parsed (forward_clustered +
   forward_mobile `scene_shader_forward_*`, `shader_types`), how a material/instance opts into the pass,
   and whether membership is material-side or per-instance (the load-bearing #7916 concern).
2. **`fold_order_sort.h` caller-order sort** — the within-pass ordering primitive: its key, where it's
   applied, how it relates to the game's `DepthMode.sorting_offset_for()` / `OTDepthPrimOrder`.
3. **Held-out target / scratch plumbing** — `render_scene_buffers_rd` + `rendering_server` additions:
   how the held-out target is allocated, seeded, and composited in-frame (the seed + resolve passes).
4. **`rendering_server.{cpp,h}` public surface** — what new RS API the branch exposes (this is the
   upstream-facing shape to evaluate against the proposal).
5. **Its own docs** — summarize `docs/adr/0001-fork-not-extension-for-compositor-fold.md`,
   `docs/compositor-fold-design.md`, `docs/compositor-fold-upstream-evaluation.md`. How does this
   branch's design differ from the settled proposal in `feature/render-to-compositor`'s
   `docs/proposal-draft-compositor-render-layer.md` (material-side membership, #7916-pass framing)?
6. **Delta to the destination** — bulleted: what already matches the settled upstream-PR shape, and
   what would need changing/cleaning to become that. This is the core input to the impl-plan decision:
   clean-up-in-place vs. reimplement on `feature/render-to-compositor`.

## Write findings to
`.scratch/compositor-spike/research/05-consume-material-output-findings.md`.

## Answer

Full inventory in `research/05-consume-material-output-findings.md`. Headline:

**Recommendation → reimplement clean on `feature/render-to-compositor`, NOT clean-up-in-place.** The
spike's membership decision is on the *opposite axis* from the settled proposal, so in-place cleanup
means subtracting most of what the spike added before adding the real thing — large churn on a branch
saturated with FFT-specific policy/naming. Reimplement clean; **port only two reusable pieces** (the
unit-tested `fold_order_sort` seam; the seed→held-out-pass→composite frame structure). **Keep the spike
as the on-GPU-verified oracle.**

1. **Membership is a `render_mode compositor_fold`** (`shader_types.cpp:245`; routed via
   `surf->shader->compositor_fold` at `render_forward_clustered.cpp:1157`) — i.e. a **shader-permutation
   axis**. ⚠️ This is *exactly* the thing the proposal warns splits #7916's GPU-driven indirect-draw
   batch. No per-instance property, no `StandardMaterial3D` participation. **Biggest divergence — see
   the nuance flagged into ticket 02.**
2. **Sort** (`fold_order_sort.h`) keys on `owner->sorting_offset` (uncapped float, stamped by the game's
   `OTDepthPrimOrder`/`DepthMode.sorting_offset_for()`), NaN→+INF, stable submission tie-break,
   unit-tested — near-drop-in for the proposal's §2. (Note: shipped code uses `sorting_offset`, superseding
   the design doc's older `render_priority` ±127 plan.)
3. **Held-out target** is compositor-*owned* via magic-string scope `RB_SCOPE_COMPOSITOR_FOLD`; userland
   allocates/seeds (Pass A), engine only folds into it (LOAD not clear, depth-test/no-write, after resolve
   / before POST_TRANSPARENT), userland composites (Pass C). Frame *shape* matches proposal; ownership +
   naming don't.
4. **Public API surface = one method**: `is_compositor_fold_supported()`. No `compositor.{cpp,h}` /
   `renderer_compositor.cpp` changes, no new Resource/enums — far smaller than the proposal's typed-resource
   surface.
5. The branch's own `compositor-fold-upstream-evaluation.md` **already predicts this reframe** ("bounce as
   a core PR; reframe use-case→general primitive; work is subtraction") but didn't implement it; ADR-0001
   chose fork-not-upstream.
6. **Diff size: 12 engine files, +389/−3** (13 w/ the test). Full branch = 34 files; rest is docs.
