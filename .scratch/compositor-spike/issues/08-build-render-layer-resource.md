# [Build · Step 3] `CompositorRenderLayer : Resource` + engine-owned allocation

Type: task
Status: open
Blocked by: —

## Question

Introduce the minimal identity-token resource and engine-owned, identity-keyed target allocation. Detail:
plan § Step 3.

- `scene/resources/compositor_render_layer.{h,cpp}` (new `Resource`): two-field identity token —
  `format : Format` (enumerated, #7916 set) + `seed_source : SeedSource` — plus `stage : EffectCallbackType`.
  NOT a policy bag; size/view_count/depth-source are structural invariants, not fields. Register in
  `register_scene_types.cpp`.
- Engine-owned allocation in `renderer_rd/storage_rd/render_scene_buffers_rd.{h,cpp}`: allocate-on-first-access
  keyed by the resource's identity (sibling to the named-texture store at
  `render_scene_buffers_rd.cpp:52-56`, freed in `cleanup()` `:134-137`). See Open Q2 (identity→NTKey keying/lifetime).

**Done:** creating a `CompositorRenderLayer`, referencing it twice, resolves to one engine-allocated texture
of the declared format at render-target size with correct view_count; class-ref doc stub added.

## Answer

<!-- filled on resolution -->
