# [Build · Step 4] Per-instance `render_layer`/`render_layer_order` + fill routing + new render list

Type: task
Status: open
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

<!-- filled on resolution -->
