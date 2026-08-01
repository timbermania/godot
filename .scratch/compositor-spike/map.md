<!-- wayfinder:map -->
# Map: Working compositor render-layer spike in the real FFT game (the sounding's proof)

## Destination

A **general material-side compositor render-layer primitive** — the settled #7916-pass +
two extensions (a caller-order key within a pass; a held-out target the effect seeds/composites
in-frame) — implemented on the engine branch `feature/render-to-compositor`, with the **FFT game's
fold system migrated onto it**, rendering the display-space fold in a **real FFT game scene**
(captured as screenshot/video).

That capture is the champion-earning proof the #7916 sounding currently cannot claim. The same
branch is the ADR-0001 **ship path**, so it is built as real (not throwaway) and is not wasted
even if upstream declines.

**Reaching the end of this map = the fold renders correctly in a real FFT scene on the new engine,
captured.** Posting the sounding / opening the upstream PR is downstream and out of scope (see below).

## Notes

**Scope decisions locked while charting (2026-07-31):**
- Build scope = **general material-side primitive** (not the thin fold-only slice). The fork = the
  PR-shaped implementation + the ship path, one artifact.
- Proof bar = **fold in the real game scene** (not a minimal derived scene, not an engine harness).

**Two repos + the reference fork:**
- Engine (build the primitive here): `/home/curry/Repos/godot-compositor-consume-material` @
  `feature/render-to-compositor`. Currently **docs only — zero engine code**. origin=`timbermania/godot`,
  upstream=`godotengine/godot` (**never push upstream**).
- Reference fork (the near-complete spike to adopt/clean up): `/home/curry/Repos/godot` @
  **`spike/compositor-consume-material-output`** — this is the branch the game actually targets
  (`render_mode compositor_fold`). Near-complete: material-side membership across forward_clustered +
  forward_mobile, a caller-order sort (`fold_order_sort.h`), scratch/held-out plumbing, and its own
  `docs/adr/0001-fork-not-extension-for-compositor-fold.md` + `compositor-fold-design.md` +
  `compositor-fold-upstream-evaluation.md`. Its ancestor `spike/forward-plus-display-space-additive`
  (`display_additive` render_mode, `RENDER_LIST_DISPLAY_ADDITIVE`, post-tonemap composite, no caller-order)
  is the simpler earlier spike. **Open question for ticket 02: clean up that branch in place vs. reimplement
  clean on `feature/render-to-compositor`.**
- Game (migrate the fold + capture proof): the `fft-monorepo` clone's fold lives on branch
  **`import-godot-game`** (`godot-learning/`), origin=`timbermania/fft-monorepo`.
  The game's fold is a **material contract** (ADR-0074): `Fold.add()` + monomorphic `*_fold` shaders +
  `FoldSurface` (Pass A/C scratch) + `EngineFoldCompositor` + `DepthMode.sorting_offset_for()` +
  `OTDepthPrimOrder`. Game normally runs stock **Godot 4.6 at `/usr/bin/godot`** (no-fold fallback);
  the fork build enables the fold (project.godot pins 4.8 + Forward+).

**Settled — do not reopen** (reference, don't restate):
- Sequencing = **build first, then sound** (user inverted this; resolved).
- Upstream design = **material-side membership** (per-instance opt-in dropped — splits reduz's
  GPU-driven batch). See `docs/research-gpu-driven-and-unification.md`,
  `docs/proposal-draft-compositor-render-layer.md`, `docs/adversarial-review-narrowed.md`,
  `docs/adversarial-review-subviewport.md`.
- Sounding draft = `docs/sounding-7916-comment-draft.md` — **NOT posted; never post to godotengine
  on the user's behalf.** Restore the "working fork proves the capability" line only once the fold
  actually renders.

**Worktree allocation (decided 2026-07-31):** the effort owns two dedicated worktrees; two others are
left for parallel agents.
- **Engine primitive** → `godot-compositor-consume-material` worktree (`feature/render-to-compositor`),
  a worktree of the `~/Repos/godot` clone — sibling to the reference-fork worktree `godot`
  (`spike/compositor-consume-material-output` is a *branch* in that same clone), so porting
  `fold_order_sort.h` + the frame shape across is trivial (shared `.git`).
- **Game-side migration + proof** → **`fft-monorepo-formation` worktree, PREPPED** on branch
  `feature/compositor-fold-migration` (cut from `import-godot-game`; fold verified present). The worktree's
  prior untracked WIP was discarded per user (2026-07-31); 3 substantive compositor-research docs
  (`COMPOSITOR_GENERALIZE_FEASIBILITY`, `FORWARD_PLUS_DISPLAYSPACE_BLEND`, `proto_ordered_fold.gd`) were
  salvaged to `research/wip-from-formation/` here as ticket-02/03 inputs. Note: `formation-screen` was NOT
  merged/preserved — it's abandoned in this worktree.
- **Left for other agents:** `fft-monorepo-game` (`import-godot-game`) and `fft-monorepo-compositor`
  (`character-catalogue-navigator`).

**This effort carries execution** (overrides wayfinder's plan-only default): the destination is a
built, observable artifact. Tickets are a mix of decision (research/grilling) and, once the plans
land, build (task) tickets graduated from the fog. Still resolve **one ticket per session** (except
research). The user prefers being **grilled** before big commitments.

**Skills:** `codebase-design` / `Plan` for the engine impl plan; `tdd` for the pure order comparator
(`tests/servers/rendering/`) + engine work; `run` / `verify` to observe the fold in-game; `grilling`
throughout. Run recipe seed for the old harness: memory note `spike-fold-run-invocation` (windowed,
NOT `--headless`; `scons platform=linuxbsd target=editor dev_build=yes -j24`).

## Decisions so far

<!-- one line per closed ticket: gist + link -->
- [Characterize the existing compositor fork's engine diff](issues/01-characterize-existing-fork-diff.md) —
  the game targets `~/Repos/godot @ spike/compositor-consume-material-output`, a near-complete
  `compositor_fold` impl (material-side membership, `fold_order_sort.h` caller-order, scratch plumbing,
  ADR-0001 + design docs). Its ancestor `spike/forward-plus-display-space-additive` is a simpler
  `display_additive`/`RENDER_LIST_DISPLAY_ADDITIVE` spike. Game-facing name = `compositor_fold`. Deep diff
  of the real branch → ticket 05.
- [Characterize the actual reference branch](issues/05-characterize-consume-material-output-branch.md) —
  research recommends **reimplement clean on `feature/render-to-compositor`, not clean-up-in-place**; port
  only the unit-tested `fold_order_sort` seam + the seed→held-out→composite frame shape; keep the spike as
  a GPU-verified oracle. ⚠️ **Crux risk:** the spike's membership is a `render_mode` (shader-permutation
  axis) — the very thing that splits reduz's GPU-driven batch; the settled "material-side" membership must
  be re-expressed batch-safely (ticket 02). Public API is tiny (one `is_compositor_fold_supported()`);
  diff is 12 engine files, +389/−3.
- [Engine implementation plan for the general primitive](issues/02-engine-implementation-plan.md) — **the
  crux resolved backwards:** batch-safety-by-construction *comes from* the shader-variant axis (reduz sorts
  by shader type), so "material-side" membership **must be** a `render_mode`, not a non-permuting flag —
  ticket 02's original "material-side but *not* a render_mode" ask was incoherent and is retired. Decisions:
  (1) membership = general `render_mode compositor_layer` (StandardMaterial3D needs a ShaderMaterial —
  accepted); (2) target identity = a `render_layer : CompositorRenderLayer` resource param on the member
  material, engine-owned allocation keyed by identity, N layers coexist, `get_layer_texture(resource)` — the
  proposal §1 API preserved. Reimplement-clean, porting only the comparator + frame shape. Plan (7 build
  steps, file:line-grounded): `.scratch/compositor-spike/plans/02-engine-implementation-plan.md`. Confirmed
  defaults: exact-`int32` order key; v1 seeds CLEAR+bound-Texture, `SCENE_COLOR` deferred (ticket 03 must
  validate the FFT seed maps to bound-Texture).
- [Build · Step 1: pure caller-order comparator](issues/06-build-order-comparator.md) — **built + green.**
  `compositor_layer_order_sort.h` (`CompositorLayerOrderComparator` + `compute_order(uint32_t*, const
  int32_t*, uint32_t)`) ported from SPIKE `fold_order_sort.h`, re-keyed `float`→`int32_t` (NaN
  canonicalization dropped — an int can't be NaN); test 5/5, 18 assertions pass under `tests=yes`.
  **Reusable finding:** `tests/SCsub` auto-globs `tests/**/*.cpp` + auto-generates `force_link.gen.h`
  from each `TEST_FORCE_LINK` — **no `test_main.cpp` edit needed** for this or future test steps.
  Uncommitted working-tree change.
- [Build · Step 2: register `compositor_layer` render_mode + shader-variant flag + Mobile gate](issues/07-build-render-mode-registration.md) —
  **built + verified.** The general `compositor_layer` render_mode registered globally
  (`shader_types.cpp`) + a `compositor_layer` shader-variant bool bound on both Forward+ and Mobile
  scene shaders (mirror of SPIKE `compositor_fold`, renamed); no routing, no behavioral change. 6
  files, +20/−0. Coverage-α / depth-write-off deliberately NOT ported (Step 6 policy). Verified
  **windowed** (positive+negative controls): Forward+ parses `compositor_layer` cleanly while a bogus
  mode errors; Mobile parses it AND fires the "Forward+ only" WARN. **Reusable:** `--rendering-method
  mobile` on one project exercises the Mobile path (no separate project); parse-check harness kept at
  `/tmp/step2-check/`. Uncommitted working-tree change.
- [Build · Step 3: `CompositorRenderLayer : Resource` + engine-owned allocation](issues/08-build-render-layer-resource.md) —
  **built + verified.** New `CompositorRenderLayer : Resource` — three declared fields only (`format`
  [#7916 set], `seed_source` [CLEAR/TEXTURE ship, SCENE_COLOR deferred], `stage`); no RID, no policy bag —
  registered in `register_scene_types.cpp`. Engine-owned target via
  `RenderSceneBuffersRD::get_compositor_layer_texture(id, RD::DataFormat)`: allocate-on-first-access under
  new scope `RB_SCOPE_COMPOSITOR_LAYER`, **reusing the existing NamedTexture store** (freed by `cleanup()`
  for free); size/view_count are structural invariants. Full class-ref XML (doctool: zero drift). 5 files.
  **Q2 resolved:** key on `get_instance_id()` (ObjectID) — a `CompositorRenderLayer` has no server RID.
  Tests 4/4, 9 assertions; build clean 32s. **Handoff to Step 4/5:** accessor compiled but uncalled (Step 5
  wires it → first pixels); Format→`RD::DataFormat` mapping is the caller's job, resolving
  `INHERIT_SCENE_COLOR` to `get_base_data_format()`. Uncommitted working-tree change.
- [Build · Step 4: per-instance props + fill routing + new render list](issues/09-build-instance-props-fill-routing.md) —
  **built + verified windowed.** Full per-instance plumbing (`GeometryInstance3D.render_layer` [Ref] +
  `render_layer_order` [int] → RS `instance_geometry_set_render_layer(RID, ObjectID, int32_t)` → scene_cull
  Instance store + re-apply → `RenderGeometryInstanceBase` fields), a new `RENDER_LIST_COMPOSITOR_LAYER`
  slot, a fill-branch routing members (`shader->compositor_layer && inst->render_layer.is_valid()`) **out of
  opaque/alpha/motion**, and `sort_by_layer_order()` over the Step-1 `compute_order`. **Q1 resolved:** dedicated
  `int32_t render_layer_order`, NOT the spike's overloaded float `sorting_offset`; pure CPU-side sort input,
  never uploaded (`_fill_instance_data` untouched). **Q2:** membership identity = resource `ObjectID`
  (`get_instance_id()`), matching Step 3's target key. Verified: member mesh **disappears** (held out),
  control renders, `compositor_layer`-without-`render_layer` still renders; ordering `{5,5,-2,0}` across 2
  layers → `-2 0 5 5`. 12 engine files, +133/−16; build clean 23s. **Also:** `GeometryInstanceDummy` needed a
  no-op `set_render_layer` override (implements the interface directly). Uncommitted working-tree change.

## Not yet specified

<!-- in-scope fog; graduates as tickets resolve -->
- **Build the engine primitive** — GRADUATED (2026-07-31, ticket 02) into 7 task tickets **06–12** (Steps
  1–7). **06 + 07 + 08 + 09 resolved.** Build frontier = **10** (dep 09 met — the held-out pass: partition
  `RENDER_LIST_COMPOSITOR_LAYER` by `render_layer` ObjectID, seed + draw each partition into
  `get_compositor_layer_texture()` on resolved depth → first pixels); 11 & 12 by 10.
- **Migrate the game fold onto the new primitive** (the game-side edits) — graduates once the
  migration plan (ticket 03) lands.
- **Build/run recipe for the game against the new engine** (analogue of `spike-fold-run-invocation`
  for the migrated game) — graduates once 01 + 03 clarify the binary and game branch.
- **Restore the "working fork proves it" line** to `docs/sounding-7916-comment-draft.md` — graduates
  once the fold renders in-scene (the last in-scope step before the destination).

## Out of scope

<!-- ruled beyond the destination; never graduates -->
- **Posting the sounding to godotengine / opening the upstream PR.** The map ends at proof-in-hand;
  posting is the user's own downstream act.
- **Re-litigating the upstream design** (material-side vs per-instance) — settled.
- **The throwaway `/tmp/spike-fold` harness** — superseded by real game integration.
