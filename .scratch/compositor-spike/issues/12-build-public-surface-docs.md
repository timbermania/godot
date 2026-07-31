# [Build · Step 7] `get_layer_texture` + `render_layers` effect property + capability method + docs

Type: task
Status: open
Blocked by: 10

## Question

The minimal public consumer surface + docs. Detail: plan § Step 7.

- `scene/resources/compositor.{h,cpp}` (`CompositorEffect`): add `render_layers : Array[CompositorRenderLayer]`
  exported property (follow the `access_resolved_color` bind pattern `compositor.cpp:48-50`) and a
  `get_layer_texture(resource)` method callable from the render callback (returns the RID, keyed by the same
  resource object — no string, no index; live-read each frame per proposal §1 / Attack #6).
- `rendering_server.{h,cpp}`: `bool is_compositor_layer_supported()` capability gate (returns
  `get_current_rendering_method() == "forward_plus"`), bound near `get_current_rendering_method()`.
- Docs: `doc/classes/CompositorRenderLayer.xml`, `CompositorEffect.xml`, `GeometryInstance3D.xml` (missing docs
  is a 🔴 in the upstream-eval).

**Done:** the minimal in-tree repro (one held-out mesh + one effect declaring the layer) works from GDScript with
no C++ knowledge; `has_method("is_compositor_layer_supported")` feature-detects on stock Godot.

## Answer

<!-- filled on resolution -->
