# [Build · Step 7] `get_layer_texture` + `render_layers` effect property + capability method + docs

Type: task
Status: resolved
Blocked by: 10
Assignee: Aaron Curry (claimed 2026-08-01)

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

**Built + verified windowed.** Commit `377b27e373` (engine + docs, +115/−0, 10 files). The last
engine step — the minimal public consumer surface.

**Three new surfaces:**
1. **`CompositorEffect.render_layers : CompositorRenderLayer[]`** — a declarative exported property
   (`MAKE_RESOURCE_TYPE_HINT`, mirrors `Compositor.compositor_effects`). Purely a declaration + the
   handle source for `get_layer_texture`; the held-out pass allocates targets from the *instances'*
   `render_layer` refs (Step 4/5), so this property is NOT pushed to the RenderingServer — no RS call,
   plain stored `TypedArray`. It's what tells the editor which effect a layer belongs to.
2. **`CompositorEffect.get_layer_texture(layer) → RID`** — resolves the target by handing back the same
   resource object (no string, no index). Valid **only during `_render_callback`**: `_call_render_callback`
   caches `const RenderData *current_render_data` for the callback's duration (render thread, non-reentrant)
   and clears it after; the accessor `ERR_FAIL`s outside a callback and returns empty RID when the layer
   drew nothing this frame.
3. **`RenderingServer.is_compositor_layer_supported()`** — `get_current_rendering_method() == "forward_plus"`.
   Feature-detect with `has_method()` → scripts load on stock builds.

**Design decision (layering):** to keep `scene/resources/compositor.cpp` out of `renderer_rd`, added a
**read-only virtual `RenderSceneBuffers::get_compositor_layer_texture(uint64_t) const`** (base returns
`RID()`; `RenderSceneBuffersRD` overrides → `has_texture ? get_texture : RID()`, **never allocates**,
distinct from the Step-3 allocating 2-arg overload the held-out pass uses). The effect calls the base
virtual through `RenderData::get_render_scene_buffers()`; the scope string + `itos(id)` key live only in
the RD override. This is a small, idiomatic base-class extension (the base is all virtuals) — not "just
binding," but the cleanest way to avoid a scene→renderer_rd dependency.

**Docs (doctool: zero drift, idempotent 2nd pass):** `CompositorEffect.xml` (`render_layers` +
`get_layer_texture`), `RenderingServer.xml` (`is_compositor_layer_supported`, with the `has_method`
feature-detect codeblock), `GeometryInstance3D.xml` (`render_layer` + `render_layer_order` — were
undocumented, a 🔴 in the upstream-eval). `CompositorRenderLayer.xml` already existed from Step 3.

**Verification** (`/tmp/step7-check`, windowed, Forward+): the minimal GDScript repro (one held-out
`compositor_layer` quad + one effect declaring the layer via `render_layers`) drives only the new surface:
`get_layer_texture(render_layers[0])` → `layer_center_RGBA=255,0,0,255` (member drawn), and **equals** the
render-side `get_texture("compositor_layer", str(id))` RID (`accessor_matches_string_path=true`).
`render_layers` round-trips (size=1, same object). Capability gate: `supported=true` on Forward+,
**`false` on `--rendering-method mobile`** (which also hard-fails per Step 6). `has_method` = true.

**Reusable:** `get_layer_texture` deliberately never allocates — an effect naming a layer no member drew
gets an empty RID, not a spuriously-created target. **Engine consequence:** with Step 7 done, engine
steps **06–12 are all resolved** — the general primitive's public surface is complete. The only remaining
engine work is ticket 13 (`TEXTURE` seed, critical path); the game migration (14/15) is otherwise unblocked
on the engine side.
