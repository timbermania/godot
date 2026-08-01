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
- [Build · Step 5: the held-out pass — first end-to-end pixels](issues/10-build-heldout-pass.md) — **built +
  verified windowed.** The pass draws `compositor_layer` members (held out, grouped by layer, caller-ordered)
  into engine-owned targets, occluded by the real scene, seeded CLEAR. Inverted the spike's stateless-LOAD
  into **engine-allocates-and-seeds** (`get_compositor_layer_texture` allocate-on-first-access + CLEAR). Edits
  (`render_forward_clustered.{h,cpp}`): compound `sort_by_layer_order` (group by layer id → contiguous runs,
  Q3 = compound-key not HashMap), repeat-group guard so instancing never spans a layer boundary, the missing
  `_fill_instance_data` + early uniform-set, and the pass itself (per-run: resolve resource → target → CLEAR
  seed → multiview FB vs resolved depth → draw sub-range via `element_offset`, `COLOR_PASS_FLAG_TRANSPARENT` =
  **depth-write off structurally**). **Grilled decisions:** format/seed via ObjectDB lookup at pass time (A1) —
  ⚠️ **render-thread read of a main-thread Resource, must push-down before the PR (Step 6)**; CLEAR-only seed
  this session (others warn+fallback to CLEAR); proof by direct RD readback. Verified `/tmp/step5-check`:
  visible→`255,0,0,255` (drawn), occluded→`0,0,0,0` (depth-occluded), `had_texture=true`. **Reusable:**
  `texture_get_data` *inside* a compositor callback **deadlocks** — `texture_copy` to an owned texture on the
  frame command list, read back from `_process` instead; a GDScript effect reads the target generically via
  `RenderSceneBuffersRD.get_texture("compositor_layer", str(id))` (no Step-7 accessor needed for verification).
  Uncommitted working-tree change.
- [Game-migration plan: point the FFT fold at the `compositor_layer` primitive](issues/03-game-migration-plan.md) —
  **resolved (grilling).** The 3-pass frame model survives; only membership/order-key/target-handle/seed-handoff
  move. **Crux:** sub/mix are on the proof's critical path and read `B` in place → CLEAR seed insufficient → the
  seed must be the **display-space scene**, supplied as a bound `TEXTURE` (Pass A authors it, engine copies into the
  separate engine-owned target before Pass B). PSX scene has **no non-folded transparent behind the fold**, so the
  opaque scene is a complete seed and the spike's composite-back clobber can't recur. **`SCENE_COLOR` ruled OUT of
  the engine** (linear-HDR vs display-space = policy leak); documented v2 extension only. **"Seed/composite at any
  point" flexibility rejected** — it's already `CompositorEffect` + the `stage` field; knobs would rebuild it as
  config + bake a blend policy = the eval's loudest 🔴; composite stays 100% userland. Membership flip
  (`compositor_fold`→`compositor_layer` + per-instance `render_layer`/`render_layer_order`), float→int32 order key
  (`render_layer_order_for = round(z/0.19)·RANK_STRIDE + rank`), coverage→shader ALPHA, autopilot feature-detects
  `is_compositor_layer_supported()`. **Critical-path consequence:** the engine `TEXTURE` seed must ship before the
  game renders — this graduates an *engine* ticket (13) that blocks the game's render ticket (15). Plan:
  `plans/03-game-migration-plan.md`. Graduated 13/14/15.
- [Build · Step 6: strip FFT policy; guards → invariants/hard-fails; push format/seed down](issues/11-build-strip-fft-policy.md) —
  **built + verified windowed.** Four items: (1) **FFT policy strip = no-op** — the clean reimpl never
  ported the spike's coverage-α/A2B10G10R10-clamp/RGB555-quantize; only structural depth-write-off is kept.
  (2) **Guards → invariants/hard-fails**: size/view_count are structural invariants *by construction* (engine
  allocates the target); resolved-depth-null is `ERR_FAIL_COND_MSG` (renderer bug, named); failed target alloc
  is `ERR_PRINT_ONCE`+skip (was silent `continue`). (3) **Mobile hard-fail (Q5 = hard-fail)**: mobile
  `WARN_PRINT_ONCE`→`ERR_PRINT_ONCE` naming the renderer; verified `--rendering-method mobile` now ERRORs,
  Forward+ still clean. (4) **Format/seed push-down (Step-5 PR-hardening)**: the render thread no longer
  dereferences the main-thread `CompositorRenderLayer` — format/seed are resolved main-thread-side and pushed
  as two `int32_t` params through `instance_geometry_set_render_layer` (FUNC3→FUNC5) onto the render instance;
  `run_layer` ObjectID is now only an identity key. `emit_changed()`+node re-push handles live inspector edits.
  13 files. Verified via the unchanged Step-5 harness (visible→`255,0,0,255`, occluded→`0,0,0,0`), mobile ERR,
  comparator tests 5/5. **The PR surface is now review-clean on the data-race axis.**
- [Define the proof scene and capture method](issues/04-proof-scene-and-capture.md) — **resolved
  (grilling).** The champion-earning proof is the **DEMI2 (E046) single-held-particle A/B fold-color
  audit re-run on the new `compositor_layer` engine** — NOT a combat screenshot. The `with − without`
  diff on flat mid-gray 128 (`tools/proto_single_held_particle.gd`, emitter 2 → seq 2 additive magenta,
  `--fs=26`) isolates the fold's exact per-channel contribution vs the PCSX oracle — proving axis **B**
  (material-shaded held-out geometry contributes correct pixels) numerically. **C** (occlusion) + **D**
  (caller-order) are already proven at the engine level by RD readback (tickets 09 + 10), so the game
  proof targets the color path only. ⚠️ **This refines the destination's "fold in a real FFT scene"
  proof bar → measured A/B diff.** Capture windowed; gated on migration (14 + 15); additive target needs
  only CLEAR seed (ticket 13 = sub/mix only). Handoff: `plans/04-demi2-proof-handoff.md`.
- [Build · Step 7: public consumer surface + docs](issues/12-build-public-surface-docs.md) — **built +
  verified windowed; the engine primitive's public surface is complete (steps 06–12 all resolved).** Commit
  `377b27e373` (+115/−0, 10 files). Three new surfaces: (1) `CompositorEffect.render_layers :
  CompositorRenderLayer[]` — declarative exported property, NOT pushed to RS (the held-out pass allocates
  from the *instances'* refs, Step 4/5); (2) `get_layer_texture(layer) → RID`, valid only during
  `_render_callback` (caches `current_render_data` for the callback's duration), resolves by the same
  resource object — no string, no index; (3) `RenderingServer.is_compositor_layer_supported()` (Forward+
  only; `has_method` feature-detect). **Layering decision:** added a read-only base virtual
  `RenderSceneBuffers::get_compositor_layer_texture(id) const` (RD overrides, **never allocates**; distinct
  from Step 3's allocating 2-arg overload) so `scene/` never depends on `renderer_rd`. Docs: CompositorEffect
  + RenderingServer + GeometryInstance3D `render_layer`/`render_layer_order` (was a 🔴 in the upstream-eval);
  doctool zero-drift, idempotent. Verified `/tmp/step7-check`: accessor returns the member-drawn target
  (`255,0,0,255`) **and equals** the render-side string-path RID; render_layers round-trips; mobile gate =
  false. **Engine remaining = ticket 13 only.**
- [Build · seed: real `TEXTURE` seed source](issues/13-build-texture-seed-source.md) — **built + verified
  windowed; the engine primitive is now COMPLETE.** Added `seed_texture : Texture2D` to
  `CompositorRenderLayer`; extended the Step-6 push-down `FUNC5`→`FUNC6` so the seed's *RenderingServer* RID
  is resolved main-thread and the render thread resolves it to the **current RD texture at pass time**
  (`texture_get_rd_texture`) — a live per-frame `Texture2DRD` works with no re-push; the render thread still
  never derefs the Resource. Gave the target `CAN_COPY_TO`; the pass now, for `SEED_SOURCE_TEXTURE`,
  **verifies format+size+layers match** (ticket-11 structural check), `texture_copy`s the seed into the
  engine-owned target per view, then draws members with **LOAD** (`DRAW_DEFAULT_ALL`) instead of clear.
  Missing/mismatched texture and the deferred `SCENE_COLOR` **warn-once + fall back to CLEAR** (never draw
  over garbage); `SCENE_COLOR` stays a documented v2 extension, no engine path. Verified `/tmp/step13-check`:
  member draws left-half only → left `255,0,0,255` (member over seed = LOAD), right `0,255,0,255` (copied
  seed survived = would be `0,0,0,0` on CLEAR); mismatch→CLEAR guard confirmed. 14 files, +102/−24; build 50s;
  comparator 5/5; doctool zero-drift. **Handoff to 15:** game Pass A must author a `Texture2DRD`
  (CAN_COPY_FROM, internal size, format matching `fold_layer.tres`) bound once as `seed_texture`.
- [Migrate · membership flip + int order key + shader rename + routing tests](issues/14-migrate-membership-order-key.md) —
  **built + verified headless (game-side); 8/8 routing/DepthMode tests + 5/5 guard tools green. No pixels yet (ticket 15).**
  User chose the **FULL** migration (all 17 `compositor_fold` render_mode decls → `compositor_layer`, not just the plan's 9
  combat shaders — the new engine rejects `compositor_fold`) + guard-tool/test migration. Order key:
  `DepthMode.sorting_offset_for` (float) → **`render_layer_order_for(order_z, rank) -> int`** = `round(order_z/0.19)·RANK_STRIDE
  + rank` (`RANK_STRIDE = 1<<10`; `FOLD_RANK_EPS` retired). Membership opt-in = **exactly 2 sites** (`Fold.add` +
  `EngineFoldCompositor`), both stamping `render_layer = Fold.FOLD_LAYER` (`preload` of new `assets/fold_layer.tres`, shared →
  one partition) + int `render_layer_order`. **fold_layer.tres:** `format=2` (FORMAT_RGB10_A2 → RD `A2B10G10R10_UNORM_PACK32`;
  the plan's name was the *DataFormat*, not the resource enum), `seed_source=2` (TEXTURE), `stage` defaults POST_TRANSPARENT.
  **D4 coverage resolved by reading the engine:** Godot maps `alpha_blend_op=ADD` for add/sub/mix (even blend_sub only
  reverse-subtracts *color*), so coverage accumulates from the shaders' existing ALPHA>0 — no functional shader change, just
  corrected the stale "engine forces ADD/ONE/ONE" comments; pixel-proof deferred to 15. **NOT touched (ticket 15):**
  `FoldSurface.gd`'s `&"compositor_fold"` scratch string. 48 files, uncommitted.

## Not yet specified

<!-- in-scope fog; graduates as tickets resolve -->
- **Build the engine primitive** — GRADUATED (2026-07-31, ticket 02) into 7 task tickets **06–12** (Steps
  1–7). **DONE — all seven resolved** (2026-08-01, Step 7). The general material-side primitive is fully
  built: comparator, `compositor_layer` render_mode, `CompositorRenderLayer` resource + engine-owned target,
  per-instance membership + fill routing, the held-out pass (first pixels), FFT-policy strip + render-thread
  hardening, and now the public consumer surface (`render_layers` / `get_layer_texture` /
  `is_compositor_layer_supported`) + class-ref docs, **plus the `TEXTURE` seed source (ticket 13).**
  **ALL ENGINE WORK IS DONE — no engine tickets remain open.** Historical note: Step 11 landed the
  render-thread hardening, so 12 was the last of the *seven*, and 13 (the seed) closed the primitive.
- **`TEXTURE` seed source** — GRADUATED (2026-08-01, ticket 03) into engine ticket **13**, promoted to
  **critical path** (the FFT proof's sub/mix modes require the display-space seed), **now RESOLVED**
  (2026-08-01): the engine copies a bound (live-updatable) `Texture2DRD` into the target and LOADs it under
  the held-out members. `SCENE_COLOR` ruled out of the engine (documented v2 extension only, not a ticket).
- **Migrate the game fold onto the new primitive** — GRADUATED (2026-08-01, ticket 03) into game tickets **14**
  (membership flip + int order key + shader rename + routing tests; **RESOLVED 2026-08-01** — full migration, all
  17 shaders renamed, 2-site opt-in via `Fold.add`/`EngineFoldCompositor`, `fold_layer.tres` created, tests + guards
  green headless) and **15** (Pass A/C retarget + seed-texture wiring + autopilot gate → first game-scene pixels).
  **With 13 + 14 both resolved, ticket 15 is now UNBLOCKED and is the sole open ticket on the map** — the last build
  step before the destination. The frame is intentionally half-migrated after 14: carriers are held out into the
  engine-owned target while Pass A/C still use the old magic-string scratch, so nothing renders until 15.
- **Build/run recipe for the game against the new engine** (analogue of `spike-fold-run-invocation`) — folded into
  ticket **15** (documented + saved as a memory note once the fold renders windowed).
- **Capture the DEMI2 A/B proof** (ticket 04 defined it; `plans/04-demi2-proof-handoff.md`) — graduates once the
  migrated game runs on the new engine (after 14 + 15). User runs it from the game repo; not yet a ticket.
- **Restore the "working fork proves it" line** to `docs/sounding-7916-comment-draft.md` — graduates
  once the DEMI2 A/B diff comes back faithful (the last in-scope step before the destination).

## Out of scope

<!-- ruled beyond the destination; never graduates -->
- **Posting the sounding to godotengine / opening the upstream PR.** The map ends at proof-in-hand;
  posting is the user's own downstream act.
- **Re-litigating the upstream design** (material-side vs per-instance) — settled.
- **The throwaway `/tmp/spike-fold` harness** — superseded by real game integration.
