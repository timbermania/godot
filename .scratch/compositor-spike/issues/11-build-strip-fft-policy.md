# [Build · Step 6] Strip FFT policy; convert guard-rails to invariants/hard-fails

Type: task
Status: resolved
Blocked by: 10
Assignee: Aaron Curry (claimed 2026-08-01)

## Question

Make the engine diff carry zero FFT-specific policy and fail-safe instead of fail-corrupt. Detail: plan § Step 6.

- **DROP** the silent coverage-α ADD/ONE/ONE override, the A2B10G10R10 display-space clamp, and the RGB555
  quantize (SPIKE `scene_shader_forward_clustered.cpp:350-390`). Keep **only** the structural depth-write-off for
  this list.
- Convert the spike's warn-and-**continue** format/size/MSAA/layer guards (SPIKE `:2505-2545`) into **structural
  invariants** (size = render-target size, view_count = scene view_count *by construction*, since the engine now
  allocates the target) or **hard-fails naming the renderer** for genuinely unsupported configs (Mobile, non-RD;
  Open Q5 — hard-fail at registration).
- Coverage, where a client needs it, becomes an explicit engine-written side attachment (Step 7), never a silent
  blend rewrite.

**Done:** engine diff carries zero FFT-specific constants; unsupported configs hard-fail with a renderer-named
error instead of warn-and-corrupt. (Fold-still-renders validated in tickets 03/04 — the FFT game re-adds its
display-space policy in its own Pass A/C effect.)

## Answer

**Built + verified windowed.** The engine diff carries zero FFT-specific policy, unsupported configs
hard-fail with a renderer-named error, and the render thread no longer dereferences a main-thread
`Resource`. Four sub-items:

**1. FFT policy strip — verified already absent (no edit).** The clean reimplementation (ticket 05
decision) never ported the spike's `scene_shader_forward_clustered.cpp:350-390` overrides — the silent
coverage-α ADD/ONE/ONE, the A2B10G10R10 display-space clamp, and the RGB555 quantize are simply not in
this tree. `scene_shader_forward_clustered.cpp` carries only the `compositor_layer` bool render-mode flag;
no clamp, no coverage rewrite. The **only** structural policy kept for the held-out list is depth-write-off
(via `COLOR_PASS_FLAG_TRANSPARENT`), which is the "never corrupts scene depth" invariant, not FFT policy.
The FFT game re-adds its display-space policy in its own userland Pass A/C effect (ticket 03).

**2. Guard-rails → structural invariants / hard-fails naming the renderer** (`render_forward_clustered.cpp`
held-out pass). The spike's warn-and-**continue** size/MSAA/view-count guards are now **structural
invariants by construction** — the engine allocates each layer target itself, so target size =
render-target size and view_count = scene view_count; no runtime check exists to drift. Remaining guards
converted from silent `continue` to loud hard-fails: the resolved-depth null check is a single
`ERR_FAIL_COND_MSG` before the run loop (a null there is a renderer bug, named), and a failed target
allocation is `ERR_PRINT_ONCE` + skip (was a silent `continue`). Non-RD is structurally excluded (the pass
is inside `RenderForwardClustered`, gated on `rb_data.is_valid()`).

**3. Mobile hard-fail at registration (Open Q5 resolved: hard-fail, not warn).**
`scene_shader_forward_mobile.cpp:193` `WARN_PRINT_ONCE` → `ERR_PRINT_ONCE`, renamed to a
renderer-named error ("not supported on the Mobile renderer (Forward+ only)… use the Forward+ rendering
method"). Verified windowed with `--rendering-method mobile`: a `render_mode compositor_layer` material now
emits a red **ERROR** (was a yellow warning); Forward+ still parses `compositor_layer` with no error.

**4. Format/seed push-down — the Step-5 PR-hardening, folded in (kills the render-thread `Resource`
read).** The Step-5 pass did `Object::cast_to<CompositorRenderLayer>(ObjectDB::get_instance(run_layer))` +
`->get_format()`/`->get_seed_source()` **on the render thread** — a data race a Godot reviewer would flag.
Now `format`/`seed_source` are resolved to plain `int32_t` enum values **on the main thread**
(`GeometryInstance3D::_update_render_layer`) and pushed down the whole chain as two extra params on
`instance_geometry_set_render_layer` (`RID, ObjectID, order, format, seed_source`), stored on the
`RendererSceneCull::Instance` (survives geometry rebuilds) and on `RenderGeometryInstanceBase`
(`render_layer_format`/`render_layer_seed_source`). The pass reads them off `run_owner` — `run_layer`
(ObjectID) is now only an identity key for `get_compositor_layer_texture`, never dereferenced. Live-edit
correctness: `CompositorRenderLayer::set_format`/`set_seed_source` now `emit_changed()`, and the node
connects that signal to re-push, so editing format/seed in the inspector takes effect.

**Files (13):** `render_forward_clustered.cpp` (pass: invariants/hard-fails + consume pushed-down values);
`scene_shader_forward_mobile.cpp` (Mobile ERR); `visual_instance_3d.{h,cpp}` (main-thread resolve +
`changed` wiring); `compositor_render_layer.cpp` (`emit_changed`); and the push-down signature chain —
`rendering_server.h`, `rendering_method.h`, `rendering_server_default.h` (FUNC3→FUNC5),
`renderer_scene_cull.{h,cpp}`, `renderer_geometry_instance.{h,cpp}`, `dummy/rasterizer_scene_dummy.h`.

**Verification (windowed, RTX 5090, Forward+):**
- Push-down end-to-end via `/tmp/step5-check` (the Step-5 harness, unchanged — it sets format/seed on the
  layer and the pass now reads the pushed-down copies): **visible** → layer center `255,0,0,255` (member
  drawn), **occluded** → `0,0,0,0` (depth-occluded), `had_texture=true`. No RD/Vulkan validation errors.
- Mobile hard-fail via `/tmp/step2-check --rendering-method mobile`: red ERROR fires; Forward+ clean.
- `--test --test-case="*CompositorLayer*"`: 5/5, 18 assertions. Build clean ~60s.

**Handoff:** Step 7 (ticket 12) is the last engine step — `get_layer_texture(resource)` accessor,
`render_layers` effect property, `is_compositor_layer_supported()` capability method, class-ref docs. The
render-thread hardening is now done, so the PR surface is review-clean on the data-race axis. (Bound-Texture
seed is ticket 13, still on the game's critical path.)
