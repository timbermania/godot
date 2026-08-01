# [Build · Step 4] Per-instance `render_layer`/`render_layer_order` + fill routing + new render list

Type: task
Status: resolved
Blocked by: 06, 07, 08

## Question

Plumb the per-instance properties and route members into a new render list ordered by the Step-1 comparator.
Detail: plan § Step 4.

- `scene/3d/visual_instance_3d.{h,cpp}` (`GeometryInstance3D`): add `render_layer : CompositorRenderLayer` +
  `render_layer_order : int` (bind + `ADD_PROPERTY`, follow the `extra_cull_margin` pattern).
- RS + `renderer_rd/forward_clustered/render_forward_clustered.*`: `geometry_instance_set_render_layer*` setters;
  new `RENDER_LIST_COMPOSITOR_LAYER` enum slot (bump `RENDER_LIST_MAX`, add the `MultiUmaBuffer` + `RenderList` slot).
- Fill-branch in `_fill_render_list` at the alpha routing site (`render_forward_clustered.cpp:1145-1157`): route
  `surf->shader->compositor_layer && inst->render_layer.is_valid()` into the new list, out of opaque/alpha (one
  more branch in a loop that already branches per surface — free, research §1a). `render_layer_order` rides the
  instance data (batch-neutral: reorders within the already-separated list only). See Open Q1 (order-key transport).
- Sort the list via `sort_by_layer_order()` (Step-1 comparator), added next to `sort_by_reverse_depth_and_priority()`.

**Done:** a held-out mesh disappears from the normal scene (routed into the new list, not yet drawn); the list
orders by `render_layer_order`.

## Answer

**Built + verified windowed. 12 engine files, +133/−16.** The per-instance membership/order plumbing
and the new render list are in; members are routed out of the color pass and ordered by the Step-1
comparator, but **not yet drawn** (Step 5).

**The chain, layer by layer (all in the target tree):**
1. **Public API — `GeometryInstance3D`** (`scene/3d/visual_instance_3d.{h,cpp}`): new
   `render_layer : CompositorRenderLayer` (Ref, `PROPERTY_HINT_RESOURCE_TYPE`) + `render_layer_order : int`,
   mirroring the `material_override` Ref-property pattern. Both setters forward to RS via one call,
   passing the resource's **`get_instance_id()` (ObjectID)** (null when unset) + the order.
2. **RS boundary:** new pure virtual `instance_geometry_set_render_layer(RID, ObjectID, int32_t)` on
   both `RenderingServer` (`rendering_server.h`) and the abstract `RenderingMethod`
   (`rendering_method.h`); `FUNC3` command-queue forwarder in `rendering_server_default.h`.
3. **Scene cull** (`renderer_scene_cull.{h,cpp}`): stores `render_layer`/`render_layer_order` on the
   `Instance` (so it survives geometry-instance rebuilds — re-applied in the `_update_dirty_instance`
   block next to `set_transparency`) and forwards to the renderer instance.
4. **Renderer instance** (`renderer_geometry_instance.{h,cpp}`): new `set_render_layer(ObjectID, int32_t)`
   virtual on `RenderGeometryInstance` + fields `render_layer` (ObjectID) / `render_layer_order` (int32_t)
   on `RenderGeometryInstanceBase` (inherited by both Forward+ and Mobile instances). No-op override added
   to `RasterizerSceneDummy::GeometryInstanceDummy` (it implements the interface directly — would otherwise
   go abstract; this bit at build time).
5. **The list + routing** (`render_forward_clustered.{h,cpp}`): new `RENDER_LIST_COMPOSITOR_LAYER` enum slot
   (`RENDER_LIST_MAX` bumped, `instance_buffer` initializer + `render_list[]` grow for free), cleared next to
   ALPHA/MOTION. Fill-branch at the `_fill_render_list` color-pass add-site wraps the existing
   opaque/alpha/motion routing in an `else`; members
   (`surf->shader->compositor_layer && inst->render_layer.is_valid()`) go **out of opaque/alpha/motion** into
   the new list. `sort_by_layer_order()` (new RenderList method) builds an `int32_t` key array from
   `owner->render_layer_order` and calls the Step-1 `compute_order()`, called right after the ALPHA sort.

**Verification (windowed, real Vulkan, `/tmp/step4-check/proj`):** positive + negative controls.
- Member mesh (`render_mode compositor_layer` + valid `render_layer`) → **disappears** (sampled pixel black);
  control mesh renders; a `compositor_layer` material **without** `render_layer` set → **still renders**
  (the `is_valid()` gate correctly requires *both* the render_mode and a valid layer).
- Ordering (temporary instrument, since the list isn't drawn yet): 4 members with keys `{5,5,-2,0}` across
  **two different** layer resources, all collected into the one list, sorted to **`-2 0 5 5`** — ascending,
  negative-first, stable-duplicate. Confirms the glue over the (already unit-tested) comparator, and that the
  list aggregates members across different layers (per-layer partition is Step 5). Instrument removed; final
  build clean (`SCONS_EXIT=0`, 23s).

**Open questions resolved:**
- **Q1 (order-key transport)** — did **not** overload `sorting_offset` (the spike did). Added a dedicated
  `int32_t render_layer_order` end to end. It is a **pure CPU-side sort input, never uploaded to the GPU**
  (like the spike's `sort_by_fold_order`), so `_fill_instance_data` was untouched.
- **Q2 (identity → renderer)** — the resource **`ObjectID`** (from `get_instance_id()`) is what reaches
  the renderer instance and gates membership via `is_valid()`. Matches Step 3's `get_compositor_layer_texture(id)`
  keying, so Step 5's partition can resolve `ObjectID → NamedTexture` directly.

**Handoff to Step 5 (held-out pass):** the list is filled + caller-ordered but undrawn, and members are
excluded from opaque/alpha/motion (so the mesh is genuinely held out). Step 5 must: partition
`RENDER_LIST_COMPOSITOR_LAYER` by `owner->render_layer` (ObjectID), and for each partition seed + draw into
`get_compositor_layer_texture(ObjectID, DataFormat)` (Step 3) on the resolved scene depth, depth-test on /
depth-write off. Shadow passes are untouched (the fill-branch is under `PASS_MODE_COLOR` only) — revisit in
Step 6 if held-out geometry should also drop out of shadows. Mobile inherits the fields but never routes
(Forward+ only, per Step 2's gate) — correct.
