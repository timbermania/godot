# Research: Compositor Extension Points for a Material-Owned, Caller-Ordered Render Target

**Repo state:** Godot engine checkout, branch `feature/render-to-compositor` off `upstream/master` @ `4e8c061c9b` (stock Godot 4.8-dev, no fork patches). All engine citations verified by reading the file at the cited line.

**The primitive under investigation:** a `ShaderMaterial` that declares a compositor-owned *named* render target and shades *itself* into that target, in a **caller-controlled** draw order (as opposed to depth-sorted transparency). The fork prototype faked this by writing into a magic-string named texture `("compositor_fold","color")` from a `CompositorEffect` callback.

---

## Executive summary (5 bullets)

- **No proposal exists for this exact primitive.** Nothing in godot-proposals describes "a ShaderMaterial declares a compositor-owned named render target and the compositor renders that material in a caller-controlled order." Its three defining features are scattered across separate, non-overlapping, mostly-open proposals — the nearest whole is **#7916 "Implement a Rendering Compositor" (OPEN)**, a much larger fixed-pipeline redesign (4 custom buffers, per-pass material assignment), not a per-material self-declared target.
- **Yes — file a proposal first.** This is a new engine primitive that cuts across the shader language (`render_mode` / a new material→target binding), `RenderSceneBuffersRD`'s texture-registration scheme, and the forward_clustered render-list/pass structure. It is squarely in "needs a proposal + core-team buy-in" territory, and it overlaps enough with #7916 that the design should be reconciled with (or explicitly positioned against) that thread rather than landing as a fork patch.
- **The real named-target API is untyped StringName `(context, name)`.** `RenderSceneBuffersRD` registers/looks-up textures purely by an `NTKey{StringName context, StringName buffer_name}` hash-map key (`render_scene_buffers_rd.h:103-122`, `:169`). There is **no** typed registration and **no** "declare a named target" API; `create_texture_from_format` is first-writer-wins by string collision (`render_scene_buffers_rd.cpp:334-336`). A typed target declaration would be a genuinely new API on top of this seam.
- **The "CompositorEffect can't invoke a ShaderMaterial" wall is confirmed.** A `CompositorEffect` is dispatched as an opaque `Callable.callv({callback_type, RenderData*})` (`renderer_scene_render_rd.cpp:313-316`). It receives a `RenderingDevice` + a shader *it* supplies via `RenderData`/`RenderSceneBuffersRD`; there is no path from a compositor effect into the scene material/pipeline system. The stored effect is just `{callback_type, Callable, flags}` (`compositor_storage.h:42-48`) — no material handle anywhere.
- **It is capability-gated to RD renderers, and even those diverge.** Compositor/CompositorEffect lives on `RendererSceneRenderRD` (`renderer_scene_render.h:44`), inherited only by Forward+ (`render_forward_clustered.h:60`) and Mobile (`render_forward_mobile.h:42`); `gl_compatibility` (`RasterizerSceneGLES3 : RendererSceneRender`, `rasterizer_scene_gles3.h:152`) never dispatches the callbacks. Even between the two RD renderers, Mobile refuses `POST_OPAQUE` effects outright and drops its subpass fast-path for pre/post-transparent effects (`render_forward_mobile.cpp:891-903`), and Mobile blends transparency inside a display-space subpass vs. Forward+'s linear-space blend — so this primitive must be capability-gated, not universal.

---

## 1. Compositor Extension Surface: what a CompositorEffect can do today

### The five callback stages

`CompositorEffect::EffectCallbackType` (`scene/resources/compositor.h:43-50`):

```cpp
enum EffectCallbackType {
    EFFECT_CALLBACK_TYPE_PRE_OPAQUE,
    EFFECT_CALLBACK_TYPE_POST_OPAQUE,
    EFFECT_CALLBACK_TYPE_POST_SKY,
    EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT,
    EFFECT_CALLBACK_TYPE_POST_TRANSPARENT,
    EFFECT_CALLBACK_TYPE_MAX
};
```

Mirrored on the server side as `RSE::CompositorEffectCallbackType` (`servers/rendering/rendering_server_enums.h:634-642`), with the extra sentinel `COMPOSITOR_EFFECT_CALLBACK_TYPE_ANY = -1`.

Opt-in resource inputs the effect can request (`scene/resources/compositor.h:57-61`), surfaced as server flags (`rendering_server_enums.h:626-632`):

```cpp
COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_COLOR   = 1,
COMPOSITOR_EFFECT_FLAG_ACCESS_RESOLVED_DEPTH   = 2,
COMPOSITOR_EFFECT_FLAG_NEEDS_MOTION_VECTORS    = 4,
COMPOSITOR_EFFECT_FLAG_NEEDS_ROUGHNESS         = 8,
COMPOSITOR_EFFECT_FLAG_NEEDS_SEPARATE_SPECULAR = 16,
```

### What each stage receives

The effect is invoked with `{p_callback_type, p_render_data}` — the callback type (int) and a `const RenderData *` (`renderer_scene_render_rd.cpp:314-316`). Through `RenderData` the effect reaches the `RenderSceneBuffersRD` (the named-texture store from §2), the `RenderingDevice`, scene/camera data, and the resolved color/depth textures it flagged for. The flags above gate whether MSAA resolve of color/depth happens *before* the callback (Forward+ prepares them conditionally: `render_forward_clustered.cpp:2104-2110`). There is no material, mesh list, or pipeline handed to the effect — only buffers and a `RenderingDevice`.

### The wall (confirmed)

The stored compositor effect is exactly this struct (`servers/rendering/storage/compositor_storage.h:42-48`):

```cpp
struct CompositorEffect {
    bool is_enabled = true;
    RSE::CompositorEffectCallbackType callback_type;
    Callable callback;
    BitField<RSE::CompositorEffectFlags> flags = {};
};
```

Dispatch is a blind `Callable` invocation (`servers/rendering/renderer_rd/renderer_scene_render_rd.cpp:298-317`):

```cpp
void RendererSceneRenderRD::_process_compositor_effects(RSE::CompositorEffectCallbackType p_callback_type, const RenderDataRD *p_render_data) {
    ...
    Vector<RID> re_rids = comp_storage->compositor_get_compositor_effects(p_render_data->compositor, p_callback_type, true);
    for (RID rid : re_rids) {
        Callable callback = comp_storage->compositor_effect_get_callback(rid);
        Array arr = { p_callback_type, p_render_data };
        callback.callv(arr);
    }
}
```

There is **no** `ShaderMaterial`, `RID material`, pipeline, or draw-list-of-scene-surfaces threaded through. A compositor effect gets a `RenderingDevice` and whatever compute/raster shader **it** supplies from GDScript/GDExtension; it cannot ask the engine to shade a `ShaderMaterial` for it. That is the wall the primitive would have to breach: to have a *material* shade itself, the material/pipeline system (not the compositor-effect system) must own the draw, because the compositor-effect surface has no material entry point at all.

---

## 2. Named-Target / RenderSceneBuffers API — the real shape

The seam the fork abused. `RenderSceneBuffersRD` stores textures under a two-part **StringName** key — nothing typed.

**The key** (`servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h:103-122`):

```cpp
struct NTKey {
    StringName context;
    StringName buffer_name;
    bool operator==(const NTKey &p_val) const { ... }
    static uint32_t hash(const NTKey &p_val) { ... }
    NTKey(const StringName &p_context, const StringName &p_texture_name) { ... }
};
```

**The store** (`render_scene_buffers_rd.h:169`): `mutable HashMap<NTKey, NamedTexture, NTKey> named_textures;`

**Public method signatures** (`render_scene_buffers_rd.h:215-223`) — every one is `(context, name)` StringName-addressed:

```cpp
bool has_texture(const StringName &p_context, const StringName &p_texture_name) const;
RID  create_texture(const StringName &p_context, const StringName &p_texture_name, const RD::DataFormat p_data_format, const uint32_t p_usage_bits, const RD::TextureSamples p_texture_samples = RD::TEXTURE_SAMPLES_1, const Size2i p_size = Size2i(0, 0), const uint32_t p_layers = 0, const uint32_t p_mipmaps = 1, bool p_unique = true, bool p_discardable = false);
RID  create_texture_from_format(const StringName &p_context, const StringName &p_texture_name, const RD::TextureFormat &p_texture_format, RD::TextureView p_view = RD::TextureView(), bool p_unique = true);
RID  create_texture_view(const StringName &p_context, const StringName &p_texture_name, const StringName &p_view_name, RD::TextureView p_view = RD::TextureView());
RID  get_texture(const StringName &p_context, const StringName &p_texture_name) const;
const RD::TextureFormat get_texture_format(const StringName &p_context, const StringName &p_texture_name) const;
RID  get_texture_slice(const StringName &p_context, const StringName &p_texture_name, const uint32_t p_layer, const uint32_t p_mipmap, const uint32_t p_layers = 1, const uint32_t p_mipmaps = 1);
```

**Registration is first-writer-wins by string collision** — there is no typed declaration, no ownership, no schema (`render_scene_buffers_rd.cpp:328-352`):

```cpp
RID RenderSceneBuffersRD::create_texture_from_format(const StringName &p_context, const StringName &p_texture_name, const RD::TextureFormat &p_texture_format, RD::TextureView p_view, bool p_unique) {
    NTKey key(p_context, p_texture_name);
    if (named_textures.has(key)) {
        return named_textures[key].texture;   // silently returns the existing one
    }
    NamedTexture &named_texture = named_textures[key];
    named_texture.format = p_texture_format;
    named_texture.texture = RD::get_singleton()->texture_create(p_texture_format, p_view);
    ...
}
```

`get_texture` is likewise a bare key lookup, erroring if absent (`render_scene_buffers_rd.cpp:391-397`):

```cpp
RID RenderSceneBuffersRD::get_texture(const StringName &p_context, const StringName &p_texture_name) const {
    NTKey key(p_context, p_texture_name);
    ERR_FAIL_COND_V(!named_textures.has(key), RID());
    return named_textures[key].texture;
}
```

**Design implication.** There is **no** existing typed-registration API to build on. `("compositor_fold","color")` "worked" only because it is an unclaimed string in an untyped namespace; any second writer to the same string silently aliases it, and any misspelled reader gets an `ERR_FAIL` empty `RID()`. A real typed target declaration (e.g. a `CompositorTarget` resource that reserves a name + format + usage, validated at registration and resolved to an `NTKey` internally) would be a **new** layer above this map — this seam gives you addressing, not contracts.

---

## 3. `render_mode` Vocabulary — capabilities, not use-cases

Spatial-shader `render_mode` keywords are registered in `servers/rendering/shader_types.cpp:238-260`:

```cpp
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("blend"), "mix", "add", "sub", "mul", "premul_alpha" });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("depth_draw"), "opaque", "always", "never" });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("depth_prepass_alpha") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("depth_test"), { "default", "disabled", "inverted" } });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("sss_mode_skin") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("cull"), "back", "front", "disabled" });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("unshaded") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("wireframe") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("diffuse"), "lambert", "lambert_wrap", "burley", "toon" });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("specular"), "schlick_ggx", "toon", "disabled" });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("skip_vertex_transform") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("world_vertex_coords") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("ensure_correct_normals") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("shadows_disabled") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("ambient_light_disabled") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("shadow_to_opacity") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("vertex_lighting") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("particle_trails") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("alpha_to_coverage") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("alpha_to_coverage_and_one") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("debug_shadow_splits") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("fog_disabled") });
shader_modes[RSE::SHADER_SPATIAL].modes.push_back({ PNAME("specular_occlusion_disabled") });
```

Every keyword names a **rendering capability or pipeline-state toggle** — blend equation, depth-draw/test policy, cull face, lighting model, coverage, vertex-transform skipping, debug/feature disables. None names a *use-case*, an *effect*, or a *destination* ("write to X", "for water", "for the fold"). This is the naming convention a new mode for the primitive should honor: it should describe *what pipeline capability* is being requested (e.g. a capability that redirects the material's output to a declared target), not the use-case the shader author has in mind. Registered here in `shader_types.cpp`; `shader_types_list.push_back("spatial")` at `shader_types.cpp:552` is the shader-type registration itself.

---

## 4. Render List / Pass structure in forward_clustered

### The render-list enum

`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h:79-83`:

```cpp
RENDER_LIST_OPAQUE,    //used for opaque objects
RENDER_LIST_MOTION,    //used for opaque objects with motion
RENDER_LIST_ALPHA,     //used for transparent objects
RENDER_LIST_SECONDARY, //used for shadows and other objects
RENDER_LIST_MAX
```

Backed by `RenderList render_list[RENDER_LIST_MAX];` (`render_forward_clustered.h:739`) and a per-list instance buffer array (`render_forward_clustered.h:409`).

### How lists are built and drawn

- **Fill:** `_fill_render_list(RENDER_LIST_OPAQUE, ...)` walks every surface once and routes it into OPAQUE / MOTION / ALPHA by material properties (`render_forward_clustered.cpp:922`, add-to-alpha at `:1151`, add-to-motion at `:1157`). Secondary (shadows) fills separately (`:2848`).
- **Sort:** opaque/motion sort by key (front-to-back-ish batching), alpha sorts by reverse depth + priority — this is the *only* ordering transparency gets today (`render_forward_clustered.cpp:1916-1918`):
  ```cpp
  render_list[RENDER_LIST_OPAQUE].sort_by_key();
  render_list[RENDER_LIST_MOTION].sort_by_key();
  render_list[RENDER_LIST_ALPHA].sort_by_reverse_depth_and_priority();
  ```
- **Draw:** each list is turned into `RenderListParameters` and submitted via `_render_list_with_draw_list(...)` (`render_forward_clustered.cpp:683`), which opens a draw list on a framebuffer and calls the templated `_render_list_template<PASS_MODE_*>` (`:306`). Opaque color pass at `:2224-2225`; transparent pass at `:2426-2427`.

### Where the transparent resolve happens and where POST_TRANSPARENT fires

The transparent pass draws into the color framebuffer (`render_forward_clustered.cpp:2412-2428`), then the MSAA **resolve** happens immediately after (`:2432-2446`, `texture_resolve_multisample` at `:2439`). The compositor callbacks bracket this region:

- `PRE_TRANSPARENT` fires just **before** the transparent draw (`render_forward_clustered.cpp:2409`).
- `POST_TRANSPARENT` fires **after** the transparent draw *and* after the resolve/SSIL-SSR copy (`render_forward_clustered.cpp:2458-2459`).

For reference the full ordered set of dispatch sites: `PRE_OPAQUE` `:2175`, `POST_OPAQUE` `:2271`, `POST_SKY` `:2333`, `PRE_TRANSPARENT` `:2409`, `POST_TRANSPARENT` `:2459`. (Color is in **linear** space through the transparent pass; `clear_color` is converted to linear at `:2040`/`:2093`, "After this point clear_color has linear encoding" at `:2099`.)

### What a new caller-ordered side list/pass would look like structurally

A caller-ordered pass would slot in as a new sibling of `RENDER_LIST_ALPHA`:

1. **Enum:** add `RENDER_LIST_<NAME>` before `RENDER_LIST_MAX` (`render_forward_clustered.h:79-83`); this auto-sizes `render_list[]`, `instance_buffer[]`, `curr_gpu_ptr[]` — but those array initializers (`render_forward_clustered.h:409`) list names positionally and must be extended.
2. **Fill:** add a routing branch in `_fill_render_list` (near the ALPHA branch, `render_forward_clustered.cpp:1151`) so materials tagged for the primitive land in the new list instead of ALPHA.
3. **Order:** replace `sort_by_reverse_depth_and_priority()` with a caller-provided ordering for this list (the whole point — the caller controls order, so sorting is either a stable pass-through of insertion order or a caller-supplied key).
4. **Draw:** add a `_render_list_with_draw_list(...)` invocation targeting the compositor-owned named target's framebuffer, placed at the caller-chosen point relative to the transparent pass and its resolve (`render_forward_clustered.cpp:2412-2446`) — i.e. this is where the material shades itself into the §2 named target, in §4-controlled order.

The structural cost is modest and localized (one enum slot, one fill branch, one sort policy, one draw site), but every touch is in the hottest render path and per-renderer (Mobile has its own `_render_scene`/subpass structure — §6).

---

## 5. Proposals / Prior Art

### Required: proposal #13406

**godot-proposals #13406** — title **"Add-DrawList-Access-to-CompositorEffect-for-GPU-Driven-Rendering"** — **state: CLOSED**.
<https://github.com/godotengine/godot-proposals/issues/13406>. Asks for opt-in **DrawList** access during a `CompositorEffect` callback so compute-generated geometry can be drawn on-GPU without a CPU round-trip. It is about **raw draw-list access for GPU-driven geometry**, *not* a material shading itself into a named target in caller order. A near-duplicate **#13405** ("Add DrawList access to CompositorEffect for GPU-driven rendering") is also **CLOSED**. *(Close-reason "completed vs not-planned" is not exposed on the rendered page; reported only as CLOSED — see caveats.)*

### Topic sweep

| # | Title | State | Overlap |
|---|-------|-------|---------|
| **7916** | Implement a Rendering Compositor | **OPEN** | **Closest neighbor.** Custom `RendererCompositor` with up to 4 custom buffers, opaque split into ordered/indexed passes, **materials assigned per pass**, materials read/write custom buffers via `hint_custom_buffer0_texture`. Covers material-per-pass + custom buffers + ordered passes — but as a whole fixed-pipeline redesign, not a per-material self-declared target. |
| **13406 / 13405** | DrawList access for CompositorEffect | **CLOSED** | Nearest on "render from a compositor effect," but geometry-from-compute; no material, no named target, no ordering. |
| **11251** | Add support for order-independent transparency (OIT) | **OPEN** | Hardware OIT (ROV / fragment-shader-interlock). Explicitly the *opposite* — order-*independence*, not caller-controlled order. |
| **13919** | Add a custom depth buffer for control over transparency sorting | **CLOSED** | Transparency-sort control via a custom depth buffer (clouds), not caller-ordered draw. |
| **15237** | Allow copies of `get_render_scene_buffers()` in compositor effects for multipass | **OPEN** | Multipass compositor via buffer copies; adjacent, no material/named-target. |
| **14092** | Hooks to customize 2D rendering (like RenderingCompositor in 3D) | **OPEN** | 2D analogue; adjacent. |
| **14655** | Expose glow buffer for use in compositors | **OPEN** | Exposes one existing buffer; not user-named targets or materials. |
| **10546** | Per-environment custom material override | **OPEN** | Env-level material override; not compositor-target-owned rendering. |
| **11264** | Expose directional light data through compositor callback | **CLOSED** | Callback data exposure only. |

Dedicated searches for "named render target" / "render-to-named-texture" / "ShaderMaterial compositor" / **WBOIT / weighted-blended** returned **no** matching proposal. The only transparency-*technique* proposal is #11251 (hardware OIT), which is philosophically opposite to caller-controlled order.

### Godot main-repo PRs

- `is:pr CompositorEffect DrawList` → **no PR** implements DrawList access from a compositor effect (consistent with #13405/#13406 closed unimplemented).
- **#80214 "Implement hooks into renderer" — MERGED** (BastiaanOlij, Feb 2024): the underlying CompositorEffect / render-hook infrastructure itself. <https://github.com/godotengine/godot/pull/80214>
- **#93236 "Add a post-process shader template for CompositorEffect" — CLOSED** (not merged).
- No open/merged PR lets a **ShaderMaterial shade itself into a compositor-owned target** or invokes a material from a `CompositorEffect`.

### Conclusion: does a proposal exist for THIS primitive? Should we file one?

**No proposal exists for this specific primitive** — "a `ShaderMaterial` declares a compositor-owned named render target and the compositor renders that material in a caller-controlled order." Its three defining features are split across separate, non-overlapping proposals, none closed-as-implemented:

- **Material-into-custom-buffer + ordered passes** → only inside **#7916 (OPEN)**, as a whole-compositor redesign.
- **Render-from-a-CompositorEffect** → **#13405 / #13406 (CLOSED)**, geometry-from-DrawList, no material/target/ordering.
- **Caller-controlled transparency ordering** → nothing; #11251 (OPEN) wants the opposite, #13919 (CLOSED) is depth-based.

**Yes — file a proposal first.** This crosses the shader language, the buffer-registration scheme, and the render-list/pass structure of at least two RD renderers; it needs core-team design agreement, and it must be explicitly reconciled with **#7916** (which already claims the "materials + custom buffers + ordered passes" design space, if via a different shape). Landing it as a fork patch on the untyped `("compositor_fold","color")` seam is exactly the kind of thing the proposal process exists to prevent.

---

## 6. Cross-Renderer Reality — capability-gated, not universal

### Compositor/CompositorEffect is RenderingDevice-only

The compositor storage and callback dispatch live on the RD scene-render base: `RendererCompositorStorage compositor_storage;` is a member of `RendererSceneRender` (`servers/rendering/renderer_scene_render.h:44`), but the dispatcher `_process_compositor_effects` is defined on `RendererSceneRenderRD` (`renderer_scene_render_rd.cpp:298`). Class hierarchy:

- **Forward+:** `class RenderForwardClustered : public RendererSceneRenderRD` (`render_forward_clustered.h:60`) — dispatches all five callbacks (§4).
- **Mobile:** `class RenderForwardMobile : public RendererSceneRenderRD` (`render_forward_mobile.h:42`) — dispatches, with restrictions (below).
- **Compatibility:** `class RasterizerSceneGLES3 : public RendererSceneRender` (`rasterizer_scene_gles3.h:152`) — does **not** derive from `RendererSceneRenderRD`, never calls `_process_compositor_effects`, and has **no compositor-effect dispatch at all**. (Its `is_compositor_effect(...)`/`compositor_effect_free(...)` at `drivers/gles3/rasterizer_scene_gles3.cpp:4505` are only the shared RID-free path via the base member, not a dispatch site.)

### The renderer whitelist

`main/main.cpp:2418`: `renderer_hints = "forward_plus,mobile";` then `renderer_hints += "gl_compatibility";` (`:2427`), with validation of `rendering_method` against `forward_plus` / `mobile` / `gl_compatibility` / `dummy` (`:2440-2443`). So the three real methods are Forward+, Mobile (both RD → compositor-capable), and Compatibility (GLES3 → not compositor-capable).

### Where the RD compositor / storage is instantiated

`RendererCompositorStorage` is constructed as a plain data member of the RD scene-render base (`servers/rendering/renderer_scene_render.h:44`; ctor/dtor `servers/rendering/storage/compositor_storage.cpp:37,41`), i.e. it is created once the RD scene renderer (Forward+ or Mobile) is created and is unreachable from the GL path. *(Note: the task named `renderer_compositor_rd.cpp` as the expected instantiation site; the storage is actually owned by the scene-render base, not by `RendererCompositor` — flagged below.)*

### Mobile diverges even from Forward+

Mobile explicitly narrows compositor support and blends transparency in a different color space, so the primitive cannot assume Forward+ behavior everywhere (`servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp:887-903`):

```cpp
bool ce_has_post_opaque = _has_compositor_effect(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_OPAQUE, p_render_data);
bool ce_has_pre_transparent = _has_compositor_effect(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT, p_render_data);
bool ce_has_post_transparent = _has_compositor_effect(RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT, p_render_data);

if (ce_has_post_opaque) {
    // As we're doing opaque and sky in subpasses we don't support this *yet*
    WARN_PRINT_ONCE("Post opaque rendering effect callback is not supported in the mobile renderer");
}
if (ce_has_pre_transparent) { merge_transparent_pass = false; using_subpass_post_process = false; }
if (ce_has_post_transparent) { using_subpass_post_process = false; }
```

Mobile does opaque + sky + (optionally) transparent + tonemap/blit as **subpasses** of a single render pass (`render_forward_mobile.cpp:270`, subpass management around `:1224-1304`), so transparent blending on Mobile happens inside a subpass chain that ends in a display-space tonemap/blit, whereas Forward+ blends transparency in **linear** space and resolves/tonemaps afterward (§4; linear-encoding note at `render_forward_clustered.cpp:2099`). A material-shades-into-a-target primitive that assumes linear-space blending and a discrete post-transparent slot would behave differently under Mobile's subpass model — hence it must be **capability-gated** (query renderer support and, on Mobile, force the non-subpass path exactly as the existing pre/post-transparent flags already do), not assumed universal.

---

## Claims I could NOT verify against a primary source

- **Close-reason (completed vs. not-planned) for closed proposals** (#13406, #13405, #11251-adjacent, #13919, #11264). The rendered GitHub issue pages expose the CLOSED state badge but not the completed/not-planned reason; states above are confirmed as open/closed/merged only, not the finer reason. *(Secondhand via the research sub-agent's page fetches; not independently re-fetched here.)*
- **All proposal/PR facts in §5** were gathered by a research sub-agent via WebFetch/WebSearch, not read from local files. The sub-agent confirmed each load-bearing state badge directly on the page, but I did not independently re-fetch them.
- **The prompt's expectation that the RD compositor is instantiated in `renderer_compositor_rd.cpp`** did not match the source: `RendererCompositorStorage` is a member of `RendererSceneRender` (`renderer_scene_render.h:44`), not instantiated in `renderer_compositor_rd.cpp` (no compositor-storage reference found there). Reported as found, not as expected.
