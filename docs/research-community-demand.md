# Research — community demand for a compositor-consumed render-layer / holdout primitive

**Task:** ground the proposed rendering primitive in evidence of real, popular community need.
**Method:** searched `github.com/godotengine/godot-proposals` per use-case; upvote counts (👍 / `reactions["+1"]`)
read directly from the GitHub REST API (`gh api …/issues/N`) on **2026-07-31**, so counts are exact, not scraped.
**The primitive (from `render-to-compositor-interface-design.md`):** mark selected material-shaded geometry, hold it
out of the normal scene lists, render it *through its real material pipeline* into a compositor-owned **named** aux
target that a `CompositorEffect` consumes, in a **caller-controlled draw order**, at a declared pipeline stage.

> **Load-bearing context change (2025):** Godot **4.5** shipped **stencil support in spatial materials**
> (proposal [#7174](https://github.com/godotengine/godot-proposals/issues/7174), 95 👍, closed; merged via
> [godot#80710](https://github.com/godotengine/godot/pull/80710), 2025-06-11) **with built-in Outline and X-Ray
> render-mode presets.** This directly softens the two highest-volume use-cases below (outlines, x-ray) — see
> "Positioning / overlap risks." The demand is real and large, but the pitch must route *around* the point-solutions
> the engine now ships, not compete with them.

## Headline table

| # | Use-case | Top proposal | Title | 👍 | State | How well our primitive serves it |
|---|----------|--------------|-------|----:|-------|----------------------------------|
| 1 | Per-object outlines / selection highlight | [#2795](https://github.com/godotengine/godot-proposals/issues/2795) | Use outlines instead of boxes for selected MeshInstances in the editor | 66 | open | Adjacent (editor-only ask); in-game outlines now served by 4.5 stencil presets |
| 2 | X-ray / see-through-walls silhouettes | [#7174](https://github.com/godotengine/godot-proposals/issues/7174) | Expose an intuitive subset of stencil operations | 95 | **closed/shipped 4.5** | Adjacent — engine now ships an X-Ray preset; we help only the depth-ordered / masked-composite variant |
| 3 | Render layers / AOVs / holdout / compositing | [#7916](https://github.com/godotengine/godot-proposals/issues/7916) | Implement a Rendering Compositor | **193** | open | **Fully (transparent counterpart)** — #7916 explicitly excludes transparent passes & names no holdout/named-target concept |
| 3b | G-buffer / AOV texture access | [#798](https://github.com/godotengine/godot-proposals/issues/798) | Access different viewport buffers through ViewportTextures ("G-buffer") | 94 | open | Partially — our named target is a caller-defined AOV, but #798 wants engine buffers not a custom render layer |
| 4 | Custom transparency ordering / NPR transparency | [#3986](https://github.com/godotengine/godot-proposals/issues/3986) | Sprite3D: Z-index, sorting layers and sorting groups | 26 | open | **Fully** — our per-instance `compositor_order` + `order_policy` is exactly manual transparent sort |
| 4b | Order-independent transparency | [#11251](https://github.com/godotengine/godot-proposals/issues/11251) | Add support for order-independent transparency (OIT) | 17 | open | Partially — our named target is the natural home for a WBOIT resolve, but we don't ship OIT |
| 5 | Object-isolated post-processing | [#7849](https://github.com/godotengine/godot-proposals/issues/7849) | Add a material-based post processing pass | **88** | open | **Fully** — isolate marked geometry into a named target, run a material/effect on just that layer |
| 6 | Render 3D object w/ real material to texture | [#7379](https://github.com/godotengine/godot-proposals/issues/7379) | Implement Drawable Textures | **136** | closed | Partially — same "material→texture without a SubViewport" need; #7379 is 2D-canvas-shaped |
| 7 | Decals / portals / planar reflections via real materials | [#7916](https://github.com/godotengine/godot-proposals/issues/7916) | Implement a Rendering Compositor | **193** | open | Overlap — #7916 already claims decals/portals/reflections for *opaque* passes; we add the transparent/holdout half |
| — | Scriptable render pipeline (context) | [#644](https://github.com/godotengine/godot-proposals/issues/644) | Add support for Customizable/Scriptable Render Pipelines | **99** | open | Adjacent — the "give me pipeline control" umbrella our primitive is a small, shippable slice of |
| — | Custom post-process workflow (context) | [#2196](https://github.com/godotengine/godot-proposals/issues/2196) | Create a more powerful and customizable workflow for custom post-processing effects | 55 | open | Adjacent — demand for exactly the CompositorEffect-consumes-a-target loop we complete |

## Per-use-case demand

**1. Per-object outlines / selection highlights.** The headline outline proposal is
[#2795](https://github.com/godotengine/godot-proposals/issues/2795) (66 👍, open, 2021) — but it is scoped to the
*editor* selection gizmo, not in-game outlines. Broader outline demand is fragmented across
[#1000](https://github.com/godotengine/godot-proposals/issues/1000) (editor outline mode, 20 👍),
[#11466](https://github.com/godotengine/godot-proposals/issues/11466) (CanvasItem outlines, 7 👍), and
[#11990](https://github.com/godotengine/godot-proposals/issues/11990) (31 👍). Crucially, in-game 3D outlines are
now served natively: **4.5's stencil Outline preset** (#7174). So the *raw* demand for outlines is large and old,
but our primitive is at best **adjacent** here — worth citing as proof the category is hot, not as our lead pitch.

**2. X-ray / see-through-walls.** No dedicated high-upvote "wallhack" proposal exists; the demand rode inside the
stencil proposal [#7174](https://github.com/godotengine/godot-proposals/issues/7174) (95 👍), which **closed and
shipped in 4.5 with a built-in X-Ray render mode.** There is a known follow-up bug (godot#107806: stencil
outline/x-ray break with transparency). Our primitive only helps the *masked-composite / depth-ordered* x-ray
variant (silhouette rendered into a named target, composited by an effect) — again **adjacent**, and now partly
pre-empted. Do not lead with x-ray.

**3. Render layers / AOVs / holdout / compositing — the strongest structural fit.** The single most-upvoted
compositor proposal, [#7916 "Implement a Rendering Compositor"](https://github.com/godotengine/godot-proposals/issues/7916)
(**193 👍**, open, 2023-09), explicitly *declines* transparent passes ("not much of a point in doing this with
transparent passes") and never names a holdout / render-layer / named-target concept — it is an opaque-pass +
custom-buffer design. That is the exact gap our primitive fills. The Blender-migrant framing has independent pull:
[#798 "G-buffer access"](https://github.com/godotengine/godot-proposals/issues/798) (94 👍, open) wants AOV-style
buffer reads, and [#2675](https://github.com/godotengine/godot-proposals/issues/2675) (12 👍) wants arbitrary
post-process buffers. (The literal "render layers" proposals are weak signals:
[#12426](https://github.com/godotengine/godot-proposals/issues/12426) 3 👍 closed,
[#4488](https://github.com/godotengine/godot-proposals/issues/4488) 0 👍 closed — the *concept* is popular, the
specific issues aren't, so cite #7916 + #798 for volume.)

**4. Custom / caller-controlled transparency ordering + NPR transparency.** Manual transparent sort is a persistent
pain: [#3986 Sprite3D sorting layers/groups](https://github.com/godotengine/godot-proposals/issues/3986) (26 👍,
open), [#7650 sort by shader parameter](https://github.com/godotengine/godot-proposals/issues/7650) (14 👍), and the
active [Transparency Sorting Mode discussion #12202]. Order-independent transparency
[#11251](https://github.com/godotengine/godot-proposals/issues/11251) (17 👍) is the "I've given up on sorting"
escape hatch. NPR/toon demand is large and adjacent:
[#1620 non-physical shading](https://github.com/godotengine/godot-proposals/issues/1620) (55 👍),
[#484 post-light cel shading](https://github.com/godotengine/godot-proposals/issues/484) (28 👍). Our per-instance
`compositor_order` + per-target `order_policy` **fully** answers the "let me control transparent draw order" ask, and
the named target is where NPR/OIT resolves would live.

**5. Object-isolated post-processing — the best mass-appeal *fit*.**
[#7849 "material-based post processing pass"](https://github.com/godotengine/godot-proposals/issues/7849) (**88 👍**,
open) and [#2196 custom post-process workflow](https://github.com/godotengine/godot-proposals/issues/2196) (55 👍) are
squarely "run an effect on a chosen slice of the scene." [#6725 per-object glow] closed at 0 👍, but the parent demand
sits in #7849/#2196. Isolating marked geometry into a named target and running a `CompositorEffect` on *just that
layer* is **exactly** this. Strong, honest, uncontested pitch.

**6. Render a 3D object w/ real material to a texture (inventory icons).**
[#7379 "Implement Drawable Textures"](https://github.com/godotengine/godot-proposals/issues/7379) (**136 👍**, closed)
and [#8766 3D UI in space](https://github.com/godotengine/godot-proposals/issues/8766) (58 👍) show large appetite for
"get a mesh's real-material output into a texture without the SubViewport dance." Our named target is that texture.
**Partial** fit — #7379 is 2D-canvas-drawing-shaped, and the SubViewport path already exists — but the "no SubViewport,
real material, direct handle" angle is a genuine draw.

**7. Decals / portals / planar reflections.** These are **already claimed** by
[#7916](https://github.com/godotengine/godot-proposals/issues/7916) for opaque passes (decals via buffer writes,
portals via camera callbacks, planar reflections via multi-render). We should *not* re-pitch them; we add the
transparent/holdout complement, and cite #7916's own opaque-only scope as the seam.

## Positioning / overlap risks

- **#7916 (193 👍) is both our biggest ally and our biggest collision.** It owns "the compositor" mindshare and
  already claims decals/portals/reflections/outlines-via-buffers — but only for **opaque** passes and with a **flat
  indexed** buffer model. Position ours as the **transparent / held-out-layer counterpart** with **named** targets
  (the design doc's load-bearing divergence, §5.3). Over-claiming decals/outlines invites "already covered by #7916."
- **4.5 stencil (#7174, shipped) pre-empts the naïve outline & x-ray pitch.** Since 4.5 ships Outline + X-Ray
  material presets, leading with "we enable outlines" reads as redundant. Reframe: our value is the *depth-ordered,
  masked, compositor-consumed* layer that stencil presets can't express (and the known transparency bug they hit).
- **Editor-outline proposals (#2795, #1000, #11990) are a different axis** — they want the *editor gizmo*, not a
  runtime rendering primitive. Cite them as category-heat evidence only; don't imply we resolve them.
- **G-buffer/AOV (#798, 94 👍) wants engine buffers, not a caller-authored layer.** Adjacent demand, but conflating
  "expose the depth/normal buffers" with "render my marked geometry into a custom target" would misrepresent both.
- **Literal "render layers" issues are low-signal** (#12426 3 👍, #4488 0 👍). The *concept* polls well via #7916/#798;
  the specific issues don't. Anchor on the high-upvote proxies, not the low-count exact-title matches.

## Mass-appeal verdict

- **The demand is real and large — but diffuse and partly already-served.** The relevant proposals clear a combined
  several-hundred upvotes (#7916 193, #7379 136, #644 99, #798 94, #7174 95, #7849 88), so the *neighborhood* is one
  of the hottest in Godot rendering. But no single "compositor-consumed holdout layer" proposal exists today — the
  need is inferred from adjacent point-asks, and 4.5 stencil already ate the shallow end (outlines/x-ray).
- **Strongest 2–3 pitches:** (1) **transparent/held-out counterpart to #7916** — the clean, uncontested gap in the
  193-upvote flagship; (2) **object-isolated post-processing** (#7849, 88 👍 + #2196, 55 👍) — exactly our named
  target + CompositorEffect loop; (3) **caller-controlled transparent draw order / NPR transparency** (#3986, #11251,
  #1620) — our order-key is a first-class answer. Lead with these three.
- **Leave to other proposals:** outlines (4.5 stencil + editor-only #2795), x-ray (4.5 stencil preset), and
  decals/portals/reflections (#7916 opaque scope). Cite them as category heat, never as features we deliver — over-
  claiming here is the fastest way to get closed as duplicate.
- **Framing mandate:** file as the explicit **"transparent / render-layer counterpart of #7916"** with **named**
  (not indexed) targets and a **caller-controlled order key**, riding #7916's mindshare while occupying the seam it
  declared out of scope. That is the honest, defensible, and least-duplicative position.

### Upvote counts flagged
All counts above were read directly from the GitHub REST API (exact, not estimated). WebFetch could **not** read
reaction counts from the rendered HTML — the `gh api` path was used instead and is authoritative.
Discussion **#12202** (Transparency Sorting Mode) is a *Discussion*, not an Issue; the REST issues endpoint does not
return its reaction count, so it is cited without a number.
