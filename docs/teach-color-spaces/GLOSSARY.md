# Color Spaces & the Fold Pipeline — Glossary

The canonical language for this course. Every lesson adheres to these terms. Terms are promoted here
only once Aaron can *use* them correctly. The terminology resolution below is a naming decision the
whole course depends on.

## Terminology resolutions (workspace conventions)

**"display space" == "screen space" == gamma space**:
In this workspace and the design docs, these three names all refer to **one** nonlinear, sRGB-family
(gamma-encoded) space. There is no fourth "display-vs-screen" color-space distinction. What differs is
the *pipeline stage* at which the same gamma encoding is applied, not the space itself:
- **display** — the gamma values used *around the fold* (seed A → fold B → out C).
- **screen** — the gamma values produced *at the very end* by tonemap, for the monitor.
_Avoid_: treating "display" and "screen" as two different color spaces.

## Color encoding

**Linear space**:
An encoding where the number is proportional to physical light — double the number, double the
photons. The encoding in which lighting, filtering, and *physically correct* blending must happen.
_Avoid_: "raw" (ambiguous — see raw texel), "HDR" (a range, not an encoding).

**Gamma space** (aka display, screen):
A nonlinear encoding (the sRGB-family curve, ≈ `light = code^2.2`) that weights code values toward the
darks for perceptual/8-bit efficiency, and that monitors expect at their input. Where the PSX
additive fold must happen.
_Avoid_: "sRGB" when you mean the whole family loosely; "corrected".

**Transfer function**:
The curve converting between the two encodings. **Decode** gamma→linear ≈ `pow(x, 2.2)`; **encode**
linear→gamma ≈ `pow(x, 1/2.2)`. They are exact inverses, so a round-trip changes representation, not
color (see [[0002-raw-texel-display-output-nuance]] for where color *does* change: the fold + quantize).

## Textures

**Texel**:
One sample read from a texture at a texture coordinate.

**Raw texel**:
A texel sampled with **no `source_color`**, so Godot applies no sRGB→linear conversion — the stored
bytes are used verbatim. In the unit atlas this is mandatory because the stored byte is a **palette
index**, not light. A raw texel is about *input integrity*, NOT about the color space of what gets
blended (that's decided later — see destination-encoding rule).

**`source_color`**:
A per-texture Godot hint meaning "this texture is gamma-encoded *light*; decode it sRGB→linear on
sample." Opt-in precisely because not all textures are light.

**Color texture vs. data texture**:
A **color** texture stores light (albedo, sprites) — gamma-encoded, decoded on input. A **data**
texture stores math inputs (normal, roughness, metallic, AO, masks, palette indices) — stored linear
/ raw and must **not** be decoded. This distinction is *why* `source_color` is opt-in.

## Depth

**Depth buffer** (z-buffer):
A per-pixel image of the nearest surface distance (near→far, 0→1), written during the opaque pass,
used to resolve occlusion without CPU sorting.

**Depth test** (read):
Compare a fragment's z against the depth buffer; discard it if it's behind. Decides whether a fragment
is *occluded*. Independent of depth write.

**Depth write / depth draw** (write):
Record a fragment's z into the depth buffer so it occludes later fragments. Decides whether a fragment
*occludes others*. `depth_draw_never` = write off (still tests); `depth_test_disabled` = test off too
(always on top).

## Routing & the fold

**Opaque pass / transparent (alpha) pass**:
The two built-in render lists. A material lands in the transparent pass if `uses_alpha_pass()` is true
— i.e. it has alpha, a non-mix blend, or *either* depth switch off. Otherwise it's opaque (and writes
depth).

**`compositor_fold`**:
The Spike-1 `render_mode` flag that peels a flagged transparent surface out of the normal alpha list
into `RENDER_LIST_COMPOSITOR_FOLD`, drawn into the display scratch a CompositorEffect consumes. The
material still shades through its real pipeline; only the destination is retargeted.

**The fold** (display-space fold):
Blending the flagged prims (`add`/`sub`/`mix`) onto a display-encoded scratch, in `OTDepthPrimOrder`
draw order, so the hardware blend happens in gamma — reproducing the PSX look.

## Load-bearing insights

**Color-space-agnostic blend**:
The hardware blender adds raw numbers; it has no notion of linear vs. gamma. A blend is a gamma blend
**iff both operands are gamma numbers** — the color space lives in the data, not the blender.

**Destination-encoding rule**:
A fragment must output in whatever encoding its destination buffer holds. Linear scene (opaque *and*
normal transparent) → output linear (`pow 2.2`). Display scratch (the fold route) → output display
(suppress the `pow`). "Which buffer am I writing into?" decides — not "am I transparent?".
