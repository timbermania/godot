# Existing fork findings — `spike/forward-plus-display-space-additive`

**Summary:** The reference fork implements the feature under the engine name `display_additive` (not `compositor_fold`); it pulls flagged transparent surfaces into a new `RENDER_LIST_DISPLAY_ADDITIVE` and re-draws them *after* tonemap into the sRGB-in-UNORM display target with hardware additive blend. It touches ~12 engine source files (~+490 lines of real logic; the rest is a Spike-2 dead-code effect helper). `compositor_fold` — the token the game shaders actually use — is the name on the *sibling* branch `spike/compositor-consume-material-output`, which is the renamed/generalized successor; it is NOT present on this branch.

Investigation basis: base/merge commit `aac1c92f5f` (last upstream merge before the spike); 6 spike commits `8631c6a570..b76e8eb2ac` (HEAD `b76e8eb2ac`). Diff read via `git diff aac1c92f5f..HEAD`. Both spike branches fork from the same base `aac1c92f5f`.

---

## 1. Naming reconciliation: `compositor_fold` vs `display_additive`

They are **the same conceptual render_mode under two different names on two different branches** — NOT an alias, and there is no `compositor_fold` parser on the target branch.

- **Engine (this branch, `spike/forward-plus-display-space-additive`)** parses only `display_additive`:
  - `servers/rendering/shader_types.cpp:245` — registers the SPATIAL render_mode: `modes.push_back({ PNAME("display_additive") });`
  - `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp:125` — binds it: `actions.render_mode_flags["display_additive"] = &display_additive;`
  - Backing flag: `scene_shader_forward_clustered.h:257` (`bool display_additive = false;`) and specialization bit `.h:131` (`uint32_t display_additive : 1;`).
  - A raw grep for `compositor_fold` across `servers/` and `scene/` on this branch returns **nothing**.

- **Game shaders** (`/home/curry/Repos/fft-monorepo-game/godot-learning/assets/shaders/*_fold.gdshader`) all say `render_mode ... compositor_fold` (e.g. `crystal_fold.gdshader:17`, `tile_decal_fold.gdshader:16`, `feedback_hud_sprite_additive_fold.gdshader:24`, plus `cursor_fold_*`, `effect_fold_*`). A material carrying `compositor_fold` would therefore **fail to parse / be ignored on this exact branch** — the game targets the newer name.

- **Where `compositor_fold` actually lives:** the sibling branch `spike/compositor-consume-material-output` renames the primitive to `compositor_fold` (`shader_types.cpp:+1`, plus `render_forward_clustered.cpp:1159 if (surf->shader->compositor_fold)`, `RENDER_LIST_COMPOSITOR_FOLD`). That branch (10 commits ahead of this one) is the generalization the game is written against. Its commit log renames the flag (`compositor_fold render_mode — self-shaded transparents into a compositor-readable scratch`) and adds a caller-order sort.

**Bottom line for the clean reimplementation:** the upstream-shaped primitive should expose the *game-facing* name `compositor_fold` (matching the sibling branch and the shipped shaders), and treat `display_additive` as the earlier internal spelling of the same mechanism.

---

## 2. `RENDER_LIST_DISPLAY_ADDITIVE` — the held-out render layer

- **Declared:** `render_forward_clustered.h:83`, inserted into the `RenderListType` enum immediately before `RENDER_LIST_MAX`:
  ```
  RENDER_LIST_ALPHA,        // transparent objects
  RENDER_LIST_SECONDARY,    // shadows/other
  RENDER_LIST_DISPLAY_ADDITIVE, // SPIKE 3: display-space additive, drawn after tonemap
  RENDER_LIST_MAX
  ```
  Its instance buffer is added to the parallel `instance_buffer[RENDER_LIST_MAX]` initializer at `render_forward_clustered.h:410` (`MultiUmaBuffer<1u>("RENDER_LIST_DISPLAY_ADDITIVE")`).

- **Routing (how a surface lands in it):** in `_fill_render_list`, `render_forward_clustered.cpp:1152-1162`. A surface that would normally go to the alpha list is instead diverted when its shader has the flag set:
  ```cpp
  if (surf->shader != nullptr && surf->shader->display_additive) {
      render_list[RENDER_LIST_DISPLAY_ADDITIVE].add_element(surf);
  } else {
      render_list[RENDER_LIST_ALPHA].add_element(surf);
  }
  ```
  Note: routing happens **only on the transparent (`FLAG_PASS_ALPHA` / `force_alpha`) path** — a `display_additive` material is treated as a transparent that is *held out* of the linear alpha pass.

- **Cleared:** `render_forward_clustered.cpp:949`, in the opaque fill block alongside the MOTION/ALPHA clears (`render_list[RENDER_LIST_DISPLAY_ADDITIVE].clear();`). This is the per-frame clear added by commit `0ae6a28587` ("clear RENDER_LIST_DISPLAY_ADDITIVE per frame").

- **Sorted + instance-data filled:** `render_forward_clustered.cpp:665` (`sort_by_reverse_depth_and_priority()`) and `:687` (`_fill_instance_data(RENDER_LIST_DISPLAY_ADDITIVE, render_info);`), mirroring the ALPHA list.

- **Drawn:** NOT during the normal transparent pass. It is drawn **after** `_render_buffers_post_process_and_tonemap()` — see §4. Ordering relative to opaque/transparent: opaque → motion → transparent(alpha) → post-process/tonemap → **display-additive**.

---

## 3. `display_additive` render_mode — pipeline / blend / attachment state

The mode does **not** select a bespoke blend pipeline. It reuses the material's own declared blend (`blend_add` / `blend_sub` / `blend_mix` in the game shaders) via the normal `PASS_MODE_COLOR` transparent pipeline. What the mode changes is **where and into what target** the draw happens, plus an sRGB encode:

- **Attachment / display-space clamp (the UNORM target):** the draw targets the render target's own RD texture, which is `R8G8B8A8_UNORM` already holding sRGB-encoded post-tonemap color:
  - `render_forward_clustered.cpp:2596-2600` — `color_texture = texture_storage->render_target_get_rd_texture(rb->get_render_target())`, `depth_texture = rb->get_depth_texture()`, framebuffer via `FramebufferCacheRD::get_cache_multiview(...)`. Because the destination is UNORM, hardware `BLEND_OP_ADD` (ONE/ONE) adds *in gamma space and saturates at 1.0* — the PSX-style display-space add that pre-tonemap linear blending cannot express.

- **sRGB-encode step:** the shader output must be gamma-encoded before the additive blend so the add is faithful:
  - Specialization flag set by the C++ side just before the draw: `render_forward_clustered.cpp:2604-2606` (`display_specialization.display_additive = 1;`).
  - Consumed in GLSL: `scene_forward_clustered_inc.glsl:151-153` declares `sc_display_additive()` (`(sc_packed_1() >> 6) & 1U`) plus a `linear_to_srgb(vec3)` helper (`inc.glsl:155-160`, IEC 61966-2-1 with a clamp to 0..1).
  - Applied in the fragment shader: `scene_forward_clustered.glsl:3104-3106` — `if (sc_display_additive()) { frag_color.rgb = linear_to_srgb(frag_color.rgb); }`. All other passes leave `frag_color` linear.

- **Multiview handling:** supported. The framebuffer is built with `get_cache_multiview(rb->get_view_count(), ...)`, the uniform set is built `is_multiview`-aware, and the draw passes `scene_data->view_count`. Comment at `render_forward_clustered.cpp:~2577` notes it lines up the MULTIVIEW shader variant but is "not yet exercised on an XR runtime." (Enabled by commit `b76e8eb2ac`.)

- **Guard: no upscaling.** The pass is skipped unless `rb->get_internal_size() == rb->get_target_size()` (`render_forward_clustered.cpp:2589`). Post-tonemap the display color is at target_size while scene depth is at internal_size, and a render pass needs all attachments at one size, so upscaled configs are deliberately unsupported (documented inline + in `docs/display-space-additive-blending.md`).

---

## 4. Post-tonemap / display composite pass — injection point & Pass A/B/C mapping

- **File / injection point:** `render_forward_clustered.cpp`, inside `_render_scene`, in the tonemap block. The additive draw is appended **immediately after** the tonemap call:
  - `render_forward_clustered.cpp:2568` — `_render_buffers_post_process_and_tonemap(p_render_data);`
  - `render_forward_clustered.cpp:2589-2608` — the display-additive draw: builds `additive_fb` from `[display color + scene depth]`, sets `display_specialization.display_additive = 1`, and calls `_render_list_with_draw_list(&render_list_params, additive_fb, RD::DRAW_DEFAULT_ALL, ...)` over `RENDER_LIST_DISPLAY_ADDITIVE`. `RENDER_TIMESTAMP("Display-space additive")` / draw label "Display-space additive pass".
  - The render-pass uniform set is set up earlier at `render_forward_clustered.cpp:2434-2438` (while render buffers are still valid) and consumed here — the bound textures are frame-pooled so the set survives across post-process.

- **Mapping onto the game's Pass A/B/C mental model:** this branch implements a **single held-out draw = the game's "Pass B" (fold resolve)**. There is only one injection site (post-tonemap), so:
  - It corresponds to the game's **POST_TRANSPARENT** resolve — the fold surfaces are composited after the ordinary transparent pass and after tonemap, i.e. the FoldSurface *resolve*.
  - There is **no separate PRE_TRANSPARENT seed pass** on this branch — no scratch is seeded from scene color, because the fold draws directly into the existing display target rather than into a separate scratch buffer. (The scratch/seed-and-resolve "Pass A → Pass B" split with a compositor-readable RID is the sibling branch's `compositor_fold`, which adds `render_scene_buffers_rd` scratch storage + `rendering_server.cpp` plumbing.) So the game's PRE_TRANSPARENT seed / POST_TRANSPARENT resolve pair collapses here into one post-tonemap composite.

- **Spike-2 dead scaffolding (note for the reimplementation):** `effects/display_space_additive.{cpp,h}` + `shaders/effects/display_space_additive.glsl` define a `DisplaySpaceAdditive` helper that draws hardcoded test quads. It is `memnew`'d/`memdelete`'d in `renderer_scene_render_rd.cpp:1877/1899` but its `draw()` is **never called** anywhere (grep for `display_space_additive->` is empty). It is proof-of-concept scaffolding from Spike 2 and is NOT part of the shipping mechanism — the real path is the RENDER_LIST + render_mode above. The clean reimplementation can drop it entirely.

---

## 5. Caller-order / within-list sorting

**This branch carries NO custom ordering mechanism.** The display-additive list is sorted with the stock alpha comparator:

- `render_forward_clustered.cpp:665` — `render_list[RENDER_LIST_DISPLAY_ADDITIVE].sort_by_reverse_depth_and_priority();` with the inline comment "*(order-independent, but harmless)*".

So ordering is left **entirely to the game** — `DepthMode.sorting_offset_for()` / `OTDepthPrimOrder` on the game side, expressed through per-material `render_priority` and depth, consumed by the standard reverse-depth-and-priority sort. There is no fold-specific caller-order pass here.

(For contrast: the sibling `compositor_fold` branch DOES add a dedicated caller-order mechanism — a new file `forward_clustered/fold_order_sort.h` (+83 lines) that folds "in caller order (render_priority), not depth" per its commit `e13d83f193`. That is a delta the clean reimplementation will need if uncapped caller-order compositing is required.)

---

## 6. Diff surface size & touched engine files

`git diff --stat aac1c92f5f..HEAD` = 18 paths, +736/-2. Of those, 6 are docs/PNG assets. **Engine-source files touched: 12** (~+490 lines of real logic; ~+320 of the +736 is the docs markdown, and ~275 of the engine lines are the never-invoked Spike-2 helper). New files marked `[new]`.

- `servers/rendering/shader_types.cpp` (+1) — register `display_additive` SPATIAL render_mode.
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp` (+2) — bind the render_mode flag + reset.
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h` (+2) — `display_additive` ShaderData bool + specialization bit.
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h` (+2) — `RENDER_LIST_DISPLAY_ADDITIVE` enum + instance buffer.
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` (+~57) — routing, per-frame clear, sort, fill, uniform-set setup, and the post-tonemap additive draw. **This is the core file.**
- `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` (+8) — sRGB-encode `frag_color` when the specialization is set.
- `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered_inc.glsl` (+14) — `sc_display_additive()` accessor + `linear_to_srgb()`.
- `servers/rendering/renderer_rd/renderer_scene_render_rd.h` (+3) — include + `DisplaySpaceAdditive *` member. *(Spike-2 scaffolding.)*
- `servers/rendering/renderer_rd/renderer_scene_render_rd.cpp` (+2) — new/delete the helper. *(Spike-2 scaffolding.)*
- `servers/rendering/renderer_rd/effects/display_space_additive.h` **[new, +75]** — Spike-2 quad-draw helper. **Dead code — never invoked.**
- `servers/rendering/renderer_rd/effects/display_space_additive.cpp` **[new, +199]** — Spike-2 quad-draw helper impl. **Dead code — never invoked.**
- `servers/rendering/renderer_rd/shaders/effects/display_space_additive.glsl` **[new, +50]** — Spike-2 helper shader. **Dead code.**

Docs (non-engine, for reference): `docs/display-space-additive-blending.md` (+320) and 5 PNGs under `docs/assets/`.

**Clean-reimplementation gauge:** the *load-bearing* surface is small — essentially 7 files: `shader_types.cpp`, the two `scene_shader_forward_clustered.*`, `render_forward_clustered.{h,cpp}`, and the two GLSL files. The three `display_space_additive.*` effect files + their two wiring points in `renderer_scene_render_rd.*` are Spike-2 dead scaffolding and can be dropped. To match the game (`compositor_fold`) and the sibling branch, the clean branch must additionally (a) rename `display_additive` → `compositor_fold`, and (b) decide whether to adopt the sibling branch's `fold_order_sort.h` caller-order sort and its scratch-buffer (seed/resolve, `render_scene_buffers_rd.*` + `rendering_server.*`) plumbing.
