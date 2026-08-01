# [Migrate · game] Membership flip + int order key + shader rename + routing tests

Type: task
Status: resolved
Blocked by: 07, 08, 09
Assignee: Aaron Curry (claimed 2026-08-01)

## Question

Move the FFT game's fold onto the new primitive's **membership + ordering**, headless-verifiable now (independent
of the seed/render work in 13/15). Game worktree: `fft-monorepo-formation/godot-learning` @
`feature/compositor-fold-migration`. Detail: `plans/03-game-migration-plan.md` D2 + D3 + D4 + D6.

- **Shaders:** rename `render_mode …, compositor_fold` → `…, compositor_layer` in
  `assets/shaders/effect_fold_{add,sub,mix}.gdshader`, `crystal_fold.gdshader`, `tile_decal_fold.gdshader`,
  `cursor_fold_{add,sub,mix}.gdshader`, `effect_callback_fold.gdshader`.
- **Coverage (D4):** the engine no longer forces ADD/ONE/ONE — each fold shader must write coverage into ALPHA for
  covered fragments and declare an alpha blend that accumulates it (so Pass C's coverage-discard still masks).
- **Per-instance opt-in:** `Fold.add()` (`src/effects/Fold.gd`) and `EngineFoldCompositor`
  (`src/effects/EngineFoldCompositor.gd`) set `render_layer = <res://assets/fold_layer.tres>` +
  `render_layer_order` on every carrier.
- **Order key (D3):** `DepthMode.sorting_offset_for` → `render_layer_order_for(order_z, rank) =
  round(order_z / 0.19) · RANK_STRIDE + rank` (int32, `RANK_STRIDE = 1 << 10`). `OTDepthPrimOrder` unchanged.
- **Layer resource:** create `res://assets/fold_layer.tres` (`format = A2B10G10R10_UNORM_PACK32`,
  `seed_source = TEXTURE`, `stage = POST_TRANSPARENT`; `seed_texture` wired in ticket 15).
- **Tests (D6):** update `CallbackFoldRoutingTest`, `TileOverlayCompositorTest`, `CrystalSpriteCompositorTest`,
  `TileCursorCompositorTest`, `FeedbackHudFoldRoutingTest`, `FormationFoldRoutingTest` (render_mode +
  `render_layer`/`render_layer_order` assertions) and `DepthModeTest` (int `render_layer_order_for`).

**Done:** all fold shaders declare `compositor_layer`; every carrier carries a valid `render_layer` + int
`render_layer_order`; the updated routing/DepthMode tests pass headless. (No pixels yet — that's ticket 15.)

## Answer

**Built + verified headless. The game's fold membership + order key are migrated onto the
`compositor_layer` primitive; all routing/DepthMode tests + guard tools are green. No pixels yet
(that's ticket 15).** 48 files changed (excl. unrelated research PNGs); worktree
`fft-monorepo-formation/godot-learning` @ `feature/compositor-fold-migration` (uncommitted).

**Scope (grilled 2026-08-01 → user chose the FULL migration):** the plan's D2 named 9 combat shaders,
but there were **17 `render_mode compositor_fold` decls** total (combat + 6 `formation_*` + feedback-HUD)
and the new engine only knows `compositor_layer`, so any un-renamed shader parse-errors on it. Chosen:
**rename all 17 + wire opt-in on all carriers + migrate the guard tools/tests too** (else the ADR-0074
color-space guards go blind on the renamed shaders).

**What shipped (D2/D3/D4/D6):**
- **Order key (D3):** `DepthMode.sorting_offset_for` (float) → **`render_layer_order_for(order_z, rank)
  -> int`** = `round(order_z / 0.19) * RANK_STRIDE + rank`, `RANK_STRIDE = 1 << 10`. `FOLD_RANK_EPS`
  retired (an int can't NaN; no ULP cliff). `OTDepthPrimOrder` untouched.
- **Membership (D2):** the opt-in surface is exactly **2 sites** — `Fold.add` (every callback / damage
  number / formation / crystal / cursor / tile-overlay carrier routes through it) and
  `EngineFoldCompositor` (the particle-pool MultiMesh carriers). Both now set `render_layer =
  FOLD_LAYER` + `render_layer_order = render_layer_order_for(...)`. **`Fold.FOLD_LAYER` =
  `preload("res://assets/fold_layer.tres")`**, shared by both → one resource → one ObjectID → one
  partition (Step-5 fast path).
- **Layer resource:** new `assets/fold_layer.tres` (`CompositorRenderLayer`): `format = 2`
  (**FORMAT_RGB10_A2**, which maps to `DATA_FORMAT_A2B10G10R10_UNORM_PACK32` — the plan's
  "A2B10G10R10" name is the RD DataFormat, not the resource enum member), `seed_source = 2`
  (**SEED_SOURCE_TEXTURE**); `stage` defaults to `POST_TRANSPARENT`; `seed_texture` wired in ticket 15.
- **Shaders:** all 17 `render_mode` decls `compositor_fold` → `compositor_layer` (+ every comment
  mention across 26 shader files).
- **Coverage (D4) — resolved by reading the engine, not by rewriting shaders:** the clean engine
  (ticket 11) dropped the spike's forced coverage-α override, but Godot's own blend modes map
  **`alpha_blend_op = ADD` for add/sub/mix** (`material_storage.cpp:651`) — even `blend_sub` accumulates
  ALPHA while only *color* reverse-subtracts. So coverage survives as long as each fold shader writes
  ALPHA>0 on covered fragments, which they already do (add/sub = 1.0, mix = 0.5). No functional shader
  change for coverage; the stale "engine forces ADD/ONE/ONE" comments in effect_fold_sub/mix were
  corrected. (Pixel-level coverage proof is ticket 15's runtime job.)
- **Tests (D6):** `DepthModeTest` re-keyed to int `render_layer_order_for` + a new negative-bucket
  across-RANK_STRIDE-boundary interleave assertion; the 6 routing tests (`CallbackFold`, `CrystalSprite`,
  `TileOverlay`, `TileCursor`, `FeedbackHud`, `FormationFold`) now assert `render_layer == FOLD_LAYER` +
  int `render_layer_order`. **8/8 PASS headless** (incl. `OTDepthPrimOrder` unchanged).
- **Guard tools:** the 5 `check_*_in_fold` / `check_compositor_routing` / `check_fold_primitive_kind`
  Python guards re-keyed to `compositor_layer` — verified they still SEE all 15 fold entries and pass
  (not blinded).

**NOT touched (ticket 15 / out of scope):** `FoldSurface.gd`'s `&"compositor_fold"` **scratch**
magic-string (Pass A/C retarget = ticket 15); the old-spike `tools/probe_*_engine_fold.gd` diagnostic
probes (not in the suite); ADR-0074/0077/CONTEXT.md prose.

**Handoff to ticket 15:** membership + order key are live, but the frame is intentionally half-migrated —
carriers are now held out into the *engine-owned* target while Pass A still seeds / Pass C still reads the
old magic-string scratch, so **nothing renders until 15 retargets Pass A/C + binds `fold_layer.tres`'s
`seed_texture`** (a `Texture2DRD`, `CAN_COPY_FROM`, internal size, format A2B10G10R10 — per ticket 13's
handoff). Run windowed (RenderingDevice required). `godot` on PATH = the 4.8 compositor build
(custom_build) — the headless suite already runs against it.
