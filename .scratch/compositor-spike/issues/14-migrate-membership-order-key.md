# [Migrate · game] Membership flip + int order key + shader rename + routing tests

Type: task
Status: open
Blocked by: 07, 08, 09

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

<!-- filled on resolution -->
