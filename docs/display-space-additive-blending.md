# Forward+ display-space (gamma-space) additive blending

**Status:** 🟡 Spike complete — mechanism proven, no shippable feature yet
**Branch:** `spike/forward-plus-display-space-additive`
**Owner:** Aaron Curry
**Last updated:** 2026-07-21

> Living document. Update the Status line and the Change log at the bottom as this
> progresses. This is engine-side R&D toward a first-class Forward+ display-space
> additive path so downstream projects don't need the CompositorEffect workaround.

---

## The one-sentence ask

Give Forward+ a way to perform **additive (and subtractive) blending of transparent
materials in display/gamma space** — where the blend arithmetic happens on tonemapped,
display-encoded values that **clamp at 1.0** — instead of today's behavior, where
`blend_add`/`blend_sub` accumulate in the **linear HDR float buffer before tonemapping**
(unclamped, wrong color space for a PSX-faithful look).

## Two axes — do not conflate them

1. **Blend *equation*** (the math): mix / add / sub / mul / reverse-subtract, custom
   factors. Covered by godot-proposals **#7058** and PR **#102366**. Not our problem —
   `blend_add` already exists.
2. **Blend *color space*** (where in the pipeline the add happens): linear-HDR vs
   display/gamma. **This is the ask.** No existing proposal frames the problem this way.

---

## Assessment (verified against 4.x source)

Every load-bearing claim in the original handoff checks out against the tree:

| Claim | Evidence |
|---|---|
| Forward+ color buffer is float, never clamps | `_render_buffers_get_preferred_color_format()` returns `R16G16B16A16_SFLOAT` — `renderer_scene_render_rd.cpp:1166` |
| `blend_add` is a HW blend in that buffer | `BLEND_OP_ADD`, `src=SRC_ALPHA`, `dst=ONE` — `material_storage.cpp:664` |
| Tonemap runs *after* the transparent pass | alpha pass → resolve → post FX → `tone_mapper->tonemapper(...)` — `renderer_scene_render_rd.cpp:~2549` |
| No post-tonemap geometry hook exists | pipeline order: opaque → `RENDER_LIST_ALPHA` (back-to-front) → resolve → post → tonemap → SMAA |
| Mobile clamps "for free" | Mobile tonemaps in-shader and writes a UNORM (display-encoded) attachment, so its HW additive blend saturates at 1.0 |

**Conclusion:** blend space is an *emergent consequence of the attachment format*, never
an exposed knob. We want it to become a knob.

### The three candidate shapes

| # | Shape | Verdict |
|---|---|---|
| 1 | Route flagged transparents into a **display-encoded attachment**, blend after tonemap, composite | ✅ **Recommended.** Faithful; true replica of Mobile. Real risks: RD pipelines are keyed to framebuffer format (scene pipelines must recompile for the target) + depth-attachment story for a 2nd geometry pass. |
| 2 | Post-tonemap **CompositorEffect** callback stage | ⚠️ Basically the game's current workaround promoted into the engine — a screen-space effect, not first-class geometry blending. Lowest risk, least faithful. |
| 3 | In-shader `blend_add_display` **render_mode** (tonemap→blend→store) | ❌ **Trap.** HW blend reads `dst` from the float attachment and adds in linear regardless of shader math. Can't clamp against a display-space `dst` you can't read without framebuffer-fetch (no portable HW path: Metal ROG / D3D12 ROV / Vulkan interlock). Not viable as stated. |

---

## Spike log

### Spike 1 — mechanism proof (2026-07-21) ✅

**Goal:** prove the color-space *clamp* mechanism cheaply — does the attachment format
alone decide whether additive saturates in Forward+?

**Change (1 functional line):** `renderer_scene_render_rd.cpp:1166`
`R16G16B16A16_SFLOAT` → `R16G16B16A16_UNORM`.

> ⚠️ First attempt edited `RenderSceneBuffersRD::get_base_data_format()`'s `force_hdr`
> branch — **dead code** for the default config. `force_hdr` only fires with viewport
> "HDR 2D" enabled (off by default); otherwise the buffer uses `preferred_data_format`,
> which is set from `_render_buffers_get_preferred_color_format()`. That function is the
> real knob. Noted for shape #1 plumbing.

**Test:** 5 overlapping `blend_add` quads, flat red `(0.4,0,0)` each → linear sum ≈ 2.0.
Glow on, `glow_hdr_threshold = 1.0`, filmic tonemap. Forward+, RTX 5090.
Project: `/tmp/spike-additive/proj` (not committed; regenerate from the snippet below).

**Result — A/B (same scene, only the format differs):**

| Baseline (float `SFLOAT`) | Spike (`UNORM`) |
|---|---|
| ![baseline](assets/additive-baseline-float.png) | ![spike](assets/additive-spike-unorm.png) |
| Large, luminous bloom halo — 2.0 survives in the buffer, feeds glow hard | Tight, starved halo — HW blend clamped each channel at 1.0 in the attachment |

**Proven:** in Forward+, the color-attachment format alone governs whether additive
saturates at white or accumulates unbounded in linear HDR. This is the "free clamp"
Mobile gets, confirmed on real hardware.

**Explicitly NOT proven / not shippable:**
- This is the **clamp** mechanism, not true gamma-space PSX add. Forward+ still tonemaps
  downstream, so the add stays **linear-space**, just clamped at 1.0.
- Flipping the *global* format caps HDR range for glow/tonemap engine-wide — throwaway.
- Faithful gamma-space add still needs **shape #1** (blend flagged transparents into a
  **post-tonemap display-encoded** target).

---

## Recommended next step — shape #1 prototype

Route only *flagged* transparents into a display-encoded attachment blended after tonemap,
then composite. Open questions this prototype must answer:

- [ ] **Pipeline format key.** Scene shader pipelines are compiled per framebuffer format.
      A second geometry pass into a UNORM/sRGB post-tonemap target needs those pipelines
      recompiled for that format. How invasive? (Look at `scene_shader_forward_clustered`
      pipeline variant keys.)
- [ ] **Depth.** The 2nd (display-space) alpha pass needs a compatible depth attachment
      for correct sorting/occlusion against the opaque scene, post-tonemap.
- [ ] **Material seam.** New `render_mode` (e.g. `blend_add_display`) → a new `BlendMode`
      enum value → routed into a separate render list. Where does the split happen in
      `render_forward_clustered`'s list fill (`RENDER_LIST_ALPHA`, ~line 1151)?
- [ ] **sRGB vs UNORM-linear target.** For true gamma-space add the target must store
      *display-encoded* (sRGB) values, i.e. the additive geometry writes post-tonemap
      color. Confirm the encode point.
- [ ] **Upstream framing.** No godot-proposal frames this as blend *space*. Consider
      filing one to fill that gap (distinct from #7058 / #102366 which are blend *mode*).

## Reproduce the spike

```gdscript
# main.gd — attach to a Node3D main scene, Forward+ renderer.
extends Node3D
const SHADER := preload("res://add.gdshader")  # blend_add, unshaded; ALBEDO = add_color
func _ready():
    var cam := Camera3D.new(); cam.position = Vector3(0,0,4); add_child(cam); cam.make_current()
    var env := Environment.new()
    env.background_mode = Environment.BG_COLOR; env.background_color = Color(0.02,0.02,0.03)
    env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
    env.glow_enabled = true; env.glow_hdr_threshold = 1.0
    var we := WorldEnvironment.new(); we.environment = env; add_child(we)
    var mat := ShaderMaterial.new(); mat.shader = SHADER
    mat.set_shader_parameter("add_color", Vector3(0.4, 0.0, 0.0))
    for i in range(5):
        var q := MeshInstance3D.new(); var m := QuadMesh.new(); m.size = Vector2(2,2)
        q.mesh = m; q.material_override = mat; q.position = Vector3(0,0,-i*0.01); add_child(q)
```

Build: `scons platform=linuxbsd target=editor dev_build=yes -jN` (~3 min on this machine).

## Reference material

- godot-proposals **#7058** — custom blend equation (`blend_custom`); blend *mode*, not space.
- godot **#102366** (PR) — "Blend Factor Specification in Shaders"; blend *mode*.
- godot-proposals **#13115** — after-glow/before-tonemap CompositorEffect hook (still pre-tonemap).
- godot-proposals **#7916** — RenderingCompositor (per-pass custom buffers in materials).
- godot-proposals **#2621 / #14344 / #12552** — related blend-mode requests.
- Docs: `tutorials/rendering/compositor.html`, `shader_reference/spatial_shader.rst`.

## Change log

- **2026-07-21** — Initial assessment + Spike 1 (mechanism proof, UNORM attachment). Clamp
  mechanism proven; shape #1 recommended for the real feature.
