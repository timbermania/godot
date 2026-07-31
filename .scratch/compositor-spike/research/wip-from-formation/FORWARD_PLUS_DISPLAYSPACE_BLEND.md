# Forward+ display-space additive blending — feasibility (no SubViewport)

**Question.** In Godot 4.6 **Forward+** (RGBA16F, linear-space HDR transparent
blending), can we — at the hardware / render-pipeline level, **without a
SubViewport** — make a *specific* additive transparent material composite its
blend in **display/gamma space** (to reproduce PSX ABR=1 `B+F` add with a
per-channel 5-bit saturating clamp), instead of in linear space?

**Method.** `/deep-research` fan-out (94 agents, 14 primary/secondary sources,
25 claims adversarially verified 3-vote). Sibling to
[`FORMATION_ORB_ADDITIVE_COLORSPACE.md`](./FORMATION_ORB_ADDITIVE_COLORSPACE.md),
which holds the already-settled linear-vs-display / PSX-hardware / gamma
physics (not re-derived here). Handoff: `/tmp/handoff-forward-plus-displayspace-blend.md`.

---

## Verdict: **YES — but only via a post-tonemap-semantics composite pass, done in explicit shader math.**

You **cannot** coerce the native Forward+ HDR blend into a display-space add +
clamp by any material flag, `render_mode`, or per-attachment blend state. The
hardware blend equation is not the lever. What *is* available, SubViewport-free,
is a **`CompositorEffect`** (RenderingDevice compute pass) that reads and writes
the HDR color buffer and performs the additive + per-channel clamp **itself, in
shader math, in display space**. The catch is the **insertion point**: every
native compositor hook in 4.6 (latest = `POST_TRANSPARENT`) runs **before**
tonemap/glow/FSR/TAA, and there is **no post-tonemap callback** in stock 4.6.

So the mechanism is real; the clean insertion point is not a built-in hook yet.
Within stock 4.6 the composite must be **self-contained** in a
`POST_TRANSPARENT` effect (pre-invert the tonemap so the gamma-encoded add
survives it), or the project disables tonemap for this path.

---

## The three load-bearing hardware facts (angles 1–3) — spec-settled, 3-0

### 1. Blend color-space is a property of the ATTACHMENT FORMAT, never a per-material / per-pass / blend-state flag
`RDPipelineColorBlendStateAttachment` exposes exactly 11 fields — `enable_blend`,
color/alpha blend ops, src/dst color/alpha factors, `write_r/g/b/a` — and **no**
field for color space, sRGB decode/encode, or clamp. It maps 1:1 to Vulkan
`VkPipelineColorBlendAttachmentState`; the sRGB decode/encode-around-blend lives
on the attachment `VkFormat` (`_SRGB` suffix), not the blend state. **There is no
`render_mode` or material flag that changes blend space.** (Answers angle 1.)
- <https://docs.godotengine.org/en/4.5/classes/class_rdpipelinecolorblendstateattachment.html>
- <https://docs.vulkan.org/spec/latest/chapters/framebuffer.html>

### 2. sRGB-format attachment ⇒ linear add; plain UNORM attachment ⇒ display-space add
- **sRGB format:** destination R,G,B **are linearized prior to blending** and the
  result **is re-encoded to sRGB before being written** (alpha never sRGB-encoded).
  Identical in Vulkan (format-driven) and OpenGL (`GL_FRAMEBUFFER_SRGB` + sRGB-capable
  attachment). ⇒ **NOT** a display-space add. (Angle 2a.)
- **plain (non-sRGB) UNORM:** "*If the format is not sRGB, no linearization is
  performed*" — stored values blend **as-is**. Hardware is color-space-agnostic; it
  adds the raw stored bit patterns. So **gamma-encoded values written into a plain
  UNORM buffer produce a display-space add** — exactly PSX ABR. (Angle 2b.)
- <https://docs.vulkan.org/spec/latest/chapters/framebuffer.html>
- <https://registry.khronos.org/OpenGL/extensions/ARB/ARB_framebuffer_sRGB.txt>
- <https://registry.khronos.org/OpenGL/extensions/EXT/EXT_framebuffer_sRGB.txt>

### 3. Fixed-point (UNORM) auto-clamps blend writes to [0,1]; float (RGBA16F) does NOT
Vulkan spec verbatim: "*If the color attachment is fixed-point, the components of
the source and destination values and blend factors are each clamped to [0,1]…
prior to evaluating the blend operations… If the color attachment is
floating-point, no clamping occurs.*" ⇒ **native Forward+ RGBA16F HDR gives no
free per-write saturation.** Reproducing PSX's per-channel 5-bit clamp therefore
**requires either a fixed-point/UNORM intermediate or an explicit `clamp()` in
the shader.** (Answers angle 3.)
- <https://docs.vulkan.org/spec/latest/chapters/framebuffer.html>
- <https://registry.khronos.org/OpenGL/extensions/ARB/ARB_framebuffer_sRGB.txt>

**Consequence for Forward+:** the native float HDR buffer gives you *neither* the
display-space add *nor* the free clamp. Both must be manufactured — either by
routing through a transient UNORM storage image (hardware does the work) or by
doing add+clamp explicitly in a compute shader.

---

## The Godot injection mechanism (angles 4–5)

### 4. `CompositorEffect` is the sanctioned, SubViewport-free hook — and it can read/write the HDR buffer
Godot 4.6 `CompositorEffect` (RenderingDevice-driven custom pass) is **"only
supported by the Mobile and Forward+ renderers,"** attaches per-viewport via
`WorldEnvironment` or `Camera3D` (no SubViewport), and binds the color image as a
**read-write `rgba16f` storage image** (`layout(rgba16f, set=0, binding=0) uniform
image2D color_image;` — "*we will be reading from and writing to it*"), dispatched
via `rd.compute_list_dispatch`. This is the "add a pass to Forward+" mechanism.
(Answers angle 4: the hook exists.)
- <https://docs.godotengine.org/en/4.6/tutorials/rendering/compositor.html>
- <https://docs.godotengine.org/en/stable/classes/class_compositoreffect.html>

**Portability footguns (Linux-relevant — this project targets Linux):**
- **`godot#108781`** — the color layer may lack `VK_IMAGE_USAGE_STORAGE_BIT` on
  some Linux/C# configs unless explicitly requested. **Pre-check this on our build.**
- **`godot#106743`** — MSAA constrains application to `POST_TRANSPARENT`.
- `CompositorEffect` is marked **Experimental** in 4.6 — API may shift.

### 5. Tonemap collision — the only native hook is PRE-tonemap
`EffectCallbackType` = `PRE_OPAQUE(0)`, `POST_OPAQUE(1)`, `POST_SKY(2)`,
`PRE_TRANSPARENT(3)`, `POST_TRANSPARENT(4)`, `MAX(5)`. `POST_TRANSPARENT` runs
"*after our transparent rendering pass, but before any built-in post-processing
effects*." Per `godot-proposals#14716`: "*Glow, auto-exposure, dof and tonemapping
also all seem to happen after POST_TRANSPARENT*" and "*FSR and TAA run after that.*"

⇒ **Writing gamma-encoded additive values into the still-linear HDR buffer at
`POST_TRANSPARENT` would be double-encoded by the downstream tonemap.** A clean
**post-tonemap** callback (`POST_FRAME`) **does not exist in 4.6** — it is only an
open proposal (`#14716`; sibling `#13115` requests an after-glow/before-tonemap
hook). Post-tonemap display-space effects (film grain, etc.) are currently
*impossible* as a stock `CompositorEffect`.
- <https://docs.godotengine.org/en/4.6/tutorials/rendering/compositor.html>
- <https://github.com/godotengine/godot-proposals/issues/14716>
- <https://github.com/godotengine/godot-proposals/issues/13115>

---

## Concrete mechanism & correct insertion point

**Correct insertion point relative to tonemap: post-tonemap, display space.**
The add+clamp is a display-space operation; it belongs *after* the buffer is
tonemapped + sRGB-encoded. Since stock 4.6 has no post-tonemap hook, realize it
one of two ways:

1. **Self-contained `POST_TRANSPARENT` `CompositorEffect` (stock 4.6):** in the
   compute shader, apply the **inverse tonemap** to the current HDR pixel to
   recover the display value the frame *will* have, do the PSX add + per-channel
   `clamp()` in that display space, then re-apply the forward tonemap so the
   downstream built-in tonemap lands the intended result. Requires knowing
   Godot's exact tonemap operator (see open Q1). Numerically fussy but stock.
2. **Disable tonemap for this project** (`tonemap_mode = LINEAR` / neutralize),
   so `POST_TRANSPARENT` *is* effectively display-adjacent and the add+clamp needs
   no inverse. Cleanest if we don't otherwise need Godot's tonemap — plausible for
   a PSX-faithful renderer.

**The clamp:** either explicit `clamp(x, 0.0, 1.0)` per channel in the compute
shader, **or** route the additive layer through a transient **UNORM** storage
image allocated via `RenderingDevice` and let hardware write-saturation do it for
free (mirrors the PSX framebuffer; see open Q2).

---

## Renderer-choice tension (flag, do not resolve here)

The project deliberately runs **Mobile** today (R10G10B10A2 UNORM auto-clamps —
memory `psx-blend-clamp-forward-plus-fork`). Forward+ is **not** cleaner for this
problem: its float HDR buffer gives neither the display-space add nor the free
clamp, and the only sanctioned pass hook is pre-tonemap. Forward+ is worth
revisiting **only if Forward+ features are independently required**; otherwise
Mobile remains the lower-friction path. This likely wants a short decision doc,
not an implementation, unless Forward+ is mandated (open Q4).

---

## Caveats & confidence

- **Angles 1–3 are rock-solid** — unanimous 3-0 on canonical primary sources
  (Vulkan spec, ARB/EXT_framebuffer_sRGB) that agree verbatim across APIs;
  not time-sensitive.
- **"Display-space add" is contingent on the app writing gamma-encoded values**
  into a non-sRGB target. Hardware is color-space-agnostic; the specs prove
  *no linearization*, but the display-space *meaning* is the app's responsibility.
- **`CompositorEffect` is Experimental** in 4.6; API may shift.
- **"No post-tonemap hook" rests on open proposals** (`#14716`, `#13115`) + docs
  — current as of 2026 but the most time-sensitive item; a future Godot could add
  `POST_FRAME` and make insertion trivial.
- **Verifier abstentions ≠ refutations.** Several "killed" claims in the raw run
  (e.g. "`CompositorEffect` can read/write the color buffer," "`POST_TRANSPARENT`
  is the latest hook") were marked 0-0 because the adversarial verifiers were
  **rate-limited into abstaining**, not because the claims are false — they are in
  fact *confirmed* by the 3-0 findings above. Do not read the raw refuted list as
  contradicting this report.

## Open questions (for implementation)
1. **Exact tonemap operator** Godot 4.6 applies (Filmic/ACES/AgX/Linear) — needed
   to pre-invert it in a `POST_TRANSPARENT` effect, or the decision to disable it.
2. Is a **transient UNORM storage image** (hardware [0,1] write-saturation) simpler
   and more faithful than an explicit `clamp()` in the compute shader?
3. Does **`godot#108781`** (missing `STORAGE` bit on Linux) hit our specific 4.6 +
   GDScript build, and what's the workaround?
4. Given Mobile already auto-clamps, is the real deliverable a **stay-on-Mobile vs
   Forward+-CompositorEffect cost/benefit decision doc** rather than an
   implementation?

## Next steps
- Per handoff: once the mechanism is chosen, use **`effect-parity`** to implement +
  verify against the live emulator (pcsx port 8080, sstate0, Ramza / cell 0;
  Godot headful, never `--headless`), and **`tdd`** to guard the composite/pass.

## Prototype results (2026-07-18, `compositor-displayspace-blend` worktree) — THE MECHANISM WORKS, BUT NOT AS COMPUTE

A throwaway `CompositorEffect` prototype was built and run headful on this build
(Godot 4.6.2, Vulkan **Forward Mobile**, NVIDIA RTX 5090, Linux). Files:
`godot-learning/tools/compositor_prototype/` (see its `README.md`). It settles the
four open questions and **overturns the report's assumed compute mechanism**.

### Finding 1 — the color layer is NOT compute-writable on Mobile (footgun #1 is fatal for compute)
`get_color_layer(0)` returns a valid RID, but its actual usage is
`usage_bits = 515 = SAMPLING | COLOR_ATTACHMENT | INPUT_ATTACHMENT`. It has **no
`STORAGE_BIT`** (can't bind as `image2D`) and **no `CAN_COPY_FROM/TO`** (can't even
`texture_copy` it into a STORAGE scratch texture). Both compute paths — direct
storage-image bind AND the copy-through-intermediate workaround — are hard-blocked
by Vulkan validation. So the report's "bind the HDR buffer as a read-write rgba16f
storage image" (from the Godot compositor *compute* tutorial) is a **Forward+
affordance that does not hold on Mobile**. godot#108781 isn't just a wrinkle here;
it's a wall.

### Finding 2 — the writable path on Mobile is a RASTER pass, and it's actually ideal
`COLOR_ATTACHMENT` is present, so the color layer **can** be written by a raster
pass. A fullscreen-triangle `CompositorEffect` with **hardware additive blend**
(`enable_blend`, `BLEND_OP_ADD`, src=`ONE` dst=`ONE`) draws the PSX foreground `F`
and lets fixed-function do `B + F`. The color layer's format is
**`63 = A2B10G10R10_UNORM_PACK32`** (10-bit UNORM) — confirming memory
`psx-blend-clamp-forward-plus-fork` — so the hardware **auto-saturates each channel
to [0,1] for free** (Vulkan fixed-point clamp, angle 3). Verified numerically: a
0.667 gray + 0.5 red reads back `(1.0, 0.667, 0.667)` — red clamped, other channels
untouched. **No storage image, no copy, no explicit `clamp()`, no SubViewport, no
renderer switch.** This is the deliverable mechanism. (Two RD gotchas found: the
pipeline must be created with `INVALID_FORMAT_ID` as the vertex format — an empty
`vertex_format_create([])` still trips "No vertex array was bound"; and the color
layer is unusable by compute per Finding 1.)

### Finding 3 — tonemap collision (footgun #2) is real and operator-sensitive; use LINEAR
`POST_TRANSPARENT` runs pre-tonemap, confirmed: the same scene gray reads back
differently per operator (Linear 0.667 / ACES 0.828 / AgX 0.639), so tonemap
demonstrably re-processes the composited buffer. Effect on the additive red:
| tonemap | RIGHT (gray 0.667 + 0.5 red) | verdict |
|---|---|---|
| **Linear (0)** | `(1.000, 0.667, 0.667)` | clean: red saturates to 1.0, G/B untouched — **PSX-faithful** |
| ACES (3) | `(1.000, 0.855, 0.839)` | crosstalk: red bleeds into G/B (baseline G was 0.828) |
| AgX (4) | `(0.929, 0.663, 0.651)` | highlight rolloff: red never reaches 1.0 + crosstalk |
⇒ **The real orb path must render with `tonemap_mode = LINEAR`** (or neutralize
tonemap), else filmic operators destroy the additive saturation. Favorable note:
the formation scene has **no `WorldEnvironment`** (grep-confirmed), so it isn't
already committed to a filmic operator — the Linear/neutral case is the default.

### Answers to the open questions
1. **Exact tonemap operator** — moot for the mechanism: render this path under
   **Linear**. The add is a *linear-space* add of the pre-tonemap buffer, not a true
   display-space add; but with Linear tonemap + the UNORM clamp the saturating
   extreme lands correctly. If exact display-space midtones ever matter, pre-invert
   sRGB in the fragment (report option 1) — not needed for a clamp-dominated orb.
2. **Transient UNORM storage image** — N/A: the color layer can't be copied to one
   (no `CAN_COPY_*`). The Mobile color attachment already *is* UNORM, so the raster
   blend gets hardware clamp directly. Simpler than any intermediate.
3. **godot#108781 on our build** — **YES, it hits us and is fatal to the compute
   path.** Workaround is not "request the STORAGE bit" (not exposed per-effect); it's
   **use a raster pass instead** (Finding 2).
4. **Stay-on-Mobile vs Forward+ decision** — **stay on Mobile.** The raster
   CompositorEffect gives display-space add + free clamp with no renderer change.
   Forward+ would trade the free UNORM clamp for a float buffer and buy nothing.

### Verdict revision
The report's top-line "YES — via a self-contained `POST_TRANSPARENT` compute pass"
stands in spirit but is wrong in mechanism on Mobile: it must be a **raster
(fixed-function additive blend) pass**, not a compute pass, and it should run under
**Linear tonemap**. Next: `effect-parity` to wire the real orb against pcsx
(port 8080, sstate0, Ramza / cell 0), reusing `DisplaySpaceAddRasterEffect.gd` as
the template. `tdd` to guard the pass.

## Sources (primary unless noted)
- Godot `RDPipelineColorBlendStateAttachment` — <https://docs.godotengine.org/en/4.5/classes/class_rdpipelinecolorblendstateattachment.html>
- Vulkan spec, framebuffer/blending — <https://docs.vulkan.org/spec/latest/chapters/framebuffer.html>
- ARB_framebuffer_sRGB — <https://registry.khronos.org/OpenGL/extensions/ARB/ARB_framebuffer_sRGB.txt>
- EXT_framebuffer_sRGB — <https://registry.khronos.org/OpenGL/extensions/EXT/EXT_framebuffer_sRGB.txt>
- Godot 4.6 Compositor tutorial — <https://docs.godotengine.org/en/4.6/tutorials/rendering/compositor.html>
- `CompositorEffect` class — <https://docs.godotengine.org/en/stable/classes/class_compositoreffect.html>
- godot-proposals#14716 (POST_FRAME request) — <https://github.com/godotengine/godot-proposals/issues/14716>
- godot-proposals#13115 (after-glow/before-tonemap request) — <https://github.com/godotengine/godot-proposals/issues/13115>
- godot#106743 (MSAA → POST_TRANSPARENT constraint) — <https://github.com/godotengine/godot/issues/106743>
