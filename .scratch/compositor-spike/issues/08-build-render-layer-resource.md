# [Build · Step 3] `CompositorRenderLayer : Resource` + engine-owned allocation

Type: task
Status: resolved
Assignee: Aaron Curry
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

**Built + verified (2026-07-31).** On `feature/render-to-compositor`. The identity token and its
engine-owned target both land; no routing yet (Step 4/5 wires the accessor to a real call site).

**What was built (5 files, +~230):**
- `scene/resources/compositor_render_layer.{h,cpp}` — new `CompositorRenderLayer : Resource`. Exactly the
  three declared fields, no policy bag: `format : Format` (enum `INHERIT_SCENE_COLOR | RGBA8 | RGB10_A2 |
  RGBA16F | R8 | R16UI` — the #7916 set), `seed_source : SeedSource` (`CLEAR | SCENE_COLOR | TEXTURE`;
  SCENE_COLOR declared-but-deferred per plan default #2), `stage : CompositorEffect::EffectCallbackType`
  (default `POST_TRANSPARENT`). Setters `ERR_FAIL_INDEX`-guard the enum range. No RID / no server call —
  it is pure identity + declarations; the engine owns allocation.
- `scene/register_scene_types.cpp` — `GDREGISTER_CLASS(CompositorRenderLayer)` next to `Compositor`.
- `render_scene_buffers_rd.{h,cpp}` — `RID get_compositor_layer_texture(uint64_t p_layer_id,
  RD::DataFormat p_data_format)`, allocate-on-first-access under new scope `RB_SCOPE_COMPOSITOR_LAYER`,
  **reusing the existing `NamedTexture` store** (so `cleanup()` frees it by construction — no new free path).
  Size = `internal_size`, layers = `view_count`, `TEXTURE_SAMPLES_1` (post-resolve), usage
  SAMPLING|COLOR_ATTACHMENT|CAN_COPY_FROM.
- `doc/classes/CompositorRenderLayer.xml` — full class-ref (not just a stub): brief + description +
  all members/constants; `--doctool` regen confirms structure matches bindings with zero drift.
- `tests/scene/test_compositor_render_layer.cpp` — 4 doctest cases (defaults, round-trip, out-of-range
  rejection, distinct-identity). Auto-globbed via `TEST_FORCE_LINK` — no `test_main.cpp` edit (ticket 06 finding).

**Verified:** `scons … tests=yes -j24` clean (32s). `--test --test-case="*CompositorRenderLayer*"` → 4/4,
9/9 assertions. `--doctool` diff = no binding drift.

**Open questions resolved:**
- **Q2 (resource-identity → NTKey):** key on `Resource::get_instance_id()` (ObjectID), stringified via
  `itos()` into a `NTKey(RB_SCOPE_COMPOSITOR_LAYER, <id>)`. Chosen over the resource RID because a
  `CompositorRenderLayer` is a plain data resource with **no server-side RID** (unlike `CompositorEffect`);
  the object id is the stable in-memory identity. The existing NamedTexture map takes a non-`SNAME`
  `StringName` key fine — no identity→scope adapter needed. Two references to the same resource → same id
  → one target (verified structurally: `create_texture_from_format` dedups on `named_textures.has(key)`).

**Handoffs to Step 4/5:**
- The accessor is **compiled but has no caller yet** (correct for Step 3). Step 5 calls it to build the
  framebuffer. Its GPU allocation path is therefore not yet exercised live — first real pixels are Step 5's
  "done".
- **Format→`RD::DataFormat` mapping is deferred to the caller (Step 4/5):** the accessor takes an already-
  resolved `RD::DataFormat`; the routing code must map `CompositorRenderLayer::Format` → RD, resolving
  `INHERIT_SCENE_COLOR` to `render_scene_buffers->get_base_data_format()`.
- Format is fixed at first access (first-writer-wins **is acceptable here** because the key IS the resource
  identity and a resource has one `format` — not the aliasing hazard the plan warned about, which was about
  distinct resources colliding).
