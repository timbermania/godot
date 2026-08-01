# [Build · seed] Real `TEXTURE` seed source (replace Step-5 CLEAR fallback)

Type: task
Status: resolved
Blocked by: 10
Assignee: Aaron Curry (claimed 2026-08-01)

## Question

Ship the bound-`TEXTURE` seed source. **Critical path** — the FFT fold's sub/mix modes read `B` in place, so a
CLEAR seed is insufficient; the game supplies its display-space scene as a bound texture (ticket 03, D1). Step 5
shipped CLEAR only (`TEXTURE` warns and falls back). Detail: `plans/03-game-migration-plan.md` D1 + ticket 13.

- Add `seed_texture : Texture2D` to `CompositorRenderLayer` (only meaningful when `seed_source == TEXTURE`).
- At pass time, before drawing the held-out partition, **copy the bound `seed_texture` into the engine-owned
  target** (replacing the CLEAR seed for this source). Format/size must match the target (structural check per
  ticket 11).
- The seed must accept a **live / per-frame-updatable RD-backed** texture (the game rewrites it each frame in its
  Pass A), not just a static imported image — confirm the accepted `Texture2D` kind (e.g. `Texture2DRD`) and that
  the engine reads the current RID at pass time.
- `SCENE_COLOR` stays **out** (linear-HDR vs display-space; would leak the linear→display transform into core) —
  document as a v2 extension only.

**Done:** a `CompositorRenderLayer` with `seed_source = TEXTURE` + a per-frame-updated `seed_texture` renders its
held-out members composited over that texture's content (verified by RD readback), with no CLEAR fallback warning.

## Answer

**Built + verified windowed. The `TEXTURE` seed source ships; the engine primitive is now complete.**
Uncommitted working-tree change (14 files, +102/−24).

**What shipped:**
- **`seed_texture : Texture2D` on `CompositorRenderLayer`** (`scene/resources/compositor_render_layer.{h,cpp}`
  + class-ref XML, doctool zero-drift). Only meaningful when `seed_source == TEXTURE`; `emit_changed()` on
  set so the node re-pushes.
- **Push-down extended `FUNC5`→`FUNC6`** — the seed texture's *RenderingServer* RID is resolved on the
  **main thread** (`GeometryInstance3D::_update_render_layer` → `Texture2D::get_rid()`) and pushed as a 6th
  param through `instance_geometry_set_render_layer` onto the render instance, exactly like Step 6's
  format/seed_source. The render thread **never dereferences the main-thread Resource** — it resolves the RS
  RID to the *current* RD texture at pass time (`TextureStorage::texture_get_rd_texture`), so a **live,
  per-frame-updated `Texture2DRD`** is read correctly as long as the same texture object stays bound
  (content or RD-RID swaps are picked up; only rebinding a *different* texture object needs a re-push, which
  `emit_changed` handles). Plumbed through `rendering_server.h`, `rendering_method.h`,
  `rendering_server_default.h`, `renderer_scene_cull.{h,cpp}` (Instance store + geometry rebuild re-apply +
  setter), `renderer_geometry_instance.{h,cpp}` (base impl + field), and the dummy rasterizer.
- **Target gained `CAN_COPY_TO`** (`render_scene_buffers_rd.cpp`) so it's a valid `texture_copy` destination
  (it already had `CAN_COPY_FROM` for consumer readback).
- **The pass** (`render_forward_clustered.cpp`): for `SEED_SOURCE_TEXTURE`, resolve the RD texture, **verify
  format + width + height + array_layers match the target** (the ticket-11 structural check), `texture_copy`
  per view into the engine-owned target, then draw members with `DRAW_DEFAULT_ALL` (**LOAD** the copied seed
  + LOAD scene depth) instead of `DRAW_CLEAR_COLOR_ALL`. CLEAR is unchanged. A missing/mismatched
  `seed_texture`, and the deferred `SCENE_COLOR`, **warn-once and fall back to CLEAR** so members never draw
  over garbage.
- **`SCENE_COLOR` stays out of the engine** — documented as a v2 extension only (no code path beyond the
  warn+fallback).

**Verification** (`/tmp/step13-check`, windowed, RD readback — the seed is size-discovered from frame 1 then
bound, exercising the live-bind path): member draws red in the left half only, right half discards. Result:
`left_RGBA = 255,0,0,255` (member composited **over** the green seed → LOAD, not clear) and
`right_RGBA = 0,255,0,255` (the copied green seed, **untouched** → the copy happened and survived the draw;
would be `0,0,0,0` on CLEAR). `RESULT=PASS`. The first run with a deliberately 128×128 seed vs the real
1261×1390 target logged the **mismatch → CLEAR fallback** warning, confirming the structural guard. Build
clean 50s; comparator tests **5/5, 18 assertions**; doctool zero-drift.

**Handoff to game ticket 15 (D1 wiring):** the game's Pass A must author its display-space seed into a
**`Texture2DRD`** whose RD texture is created with `TEXTURE_USAGE_CAN_COPY_FROM_BIT`, at the scene's internal
render size, format matching `fold_layer.tres`'s `format` (A2B10G10R10 → seed must be
`DATA_FORMAT_A2B10G10R10_UNORM_PACK32`), and bind it once as `fold_layer.tres.seed_texture`. Rewriting its
contents each frame needs no re-push. A size/format mismatch silently falls back to CLEAR (watch for the
warn), which for sub/mix would read black instead of the scene — so the match is load-bearing for the proof.
