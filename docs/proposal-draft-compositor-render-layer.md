# PROPOSAL DRAFT — Compositor render layers: render selected material-shaded geometry into a compositor-owned target, in caller order

> **Status:** draft for `godotengine/godot-proposals`. Follows the official proposal template (the six headers).
> Positioned as the **transparent / render-layer counterpart of [#7916](https://github.com/godotengine/godot-proposals/issues/7916)**.
> Supporting analysis lives in this repo: `render-to-compositor-interface-design.md` (interface),
> `research-compositor-extension-points.md` (engine seams), `research-community-demand.md` (demand evidence).

---

## Describe the project you are working on

A stylized 3D game whose look depends on compositing **material-shaded** geometry at a controlled point in the
frame — geometry that is drawn through its real Godot material (including `.gdshaderinc` includes), held out of
the normal scene, and combined by a `CompositorEffect`. The concrete client is a PSX display-space "fold," but
the need is general: **an effect that must consume geometry shaded by a Godot material, isolated into its own
buffer, in an order the author controls.**

## Describe the problem or limitation you are having in your project

A `CompositorEffect` runs raw `RenderingDevice` passes with a shader **it** supplies. It **cannot invoke a
`ShaderMaterial`** — the callback receives only `{callback_type, RenderData}` and a `RenderingDevice`; there is
no material, mesh list, or scene pipeline reachable from it (`renderer_scene_render_rd.cpp:298-317`;
`compositor_storage.h:42-48`). So anything shaded by a Godot material that a compositor wants to composite must
today be **hand-ported to GLSL and kept byte-for-byte synced with the material forever**. In our project that is
~940 lines of `.gdshaderinc` re-implemented in a compute shader; every material tweak must be mirrored by hand.

This is not project-specific. The same wall blocks a whole family of frequently-requested effects that need to
render *specific* material-shaded objects into their *own* buffer for a compositor to consume:

- **Object-isolated post-processing** — run an effect on *just these* objects
  ([#7849](https://github.com/godotengine/godot-proposals/issues/7849), 88 👍;
  [#2196](https://github.com/godotengine/godot-proposals/issues/2196), 55 👍).
- **Caller-controlled / stylized (NPR/toon) transparency order** — hand-ordered transparent layers, decals
  ([#3986](https://github.com/godotengine/godot-proposals/issues/3986)), and the resolve target a WBOIT pass
  would write into ([#11251](https://github.com/godotengine/godot-proposals/issues/11251)).
- **Render layers / holdouts / AOVs** for compositing — the transparent half of the Rendering Compositor
  ([#7916](https://github.com/godotengine/godot-proposals/issues/7916), 193 👍), which **explicitly declines
  transparent passes** ("there is not much of a point… transparency is sorted from back to front and rendered
  within a single render pass") and names no holdout/named-target concept.

The unifying missing capability: **the engine is the only thing that can shade a Godot material; the compositor
is the only thing that owns an auxiliary target — and nothing bridges them.**

## Describe the feature and how it helps overcome the problem

Add a **compositor render layer**: mark a `VisualInstance3D` to be **held out** of the normal scene and instead
rendered — through its **real material pipeline** — into a **compositor-owned, named auxiliary target**, at a
declared pipeline stage, in a **caller-controlled order**. A `CompositorEffect` declares and consumes that
target.

This dissolves the wall: a material shades *itself* into the target the effect reads, replacing hundreds of
lines of hand-synced GLSL with a property and a target declaration. Because opt-in is a **per-instance
property** (not a shader `render_mode`), **any material participates unchanged — including `StandardMaterial3D`
— with no shader authoring**, which is what makes it a general render-layer primitive rather than a shader
trick. It is deliberately the **transparent / render-layer counterpart of #7916**: reuse that proposal's
conventions where they compose (compositor-declared enumerated-format buffers, the `CUSTOM_BUFFER0..N`
aux-output vocabulary), fill the seam it declared out of scope (caller-ordered, held-out, transparent-capable
layers into named targets).

## Describe how your proposal will work (API, pseudo-code, diagrams)

### Frame model — a deferred holdout layer (not a mid-pipeline reroute)

```
render full scene EXCLUDING held-out instances       (normal engine render, to completion)
  → seed the layer target per seed_source            (CLEAR · resolved SCENE_COLOR · a bound Texture)
  → render held-out instances into the layer target  (own pass · caller order · depth-test vs resolved scene depth · depth-write OFF)
  → CompositorEffect composites layer → scene         (at the layer's declared stage)
  → output
```

This is a **post-resolve, pre-compositor-effect pass**: the held-out instances render as a **self-contained
deferred pass** hung off the compositor stage-dispatch point (drawn just *before* the effects for that stage run),
not spliced into the opaque/transparent draw loop. It runs late enough to have the full frame as context (resolved
scene color + depth, so occlusion and a `SCENE_COLOR` seed both work) yet before the effect that consumes it.
Default stage is **after the full scene render** (post-resolve); the layer may declare an **earlier** stage when it
needs MSAA'd layer geometry (see cross-renderer notes). One residual coupling to state plainly: the held-out
instances must be **gathered during the main fill** (culled, materials bound) so this later pass can draw them — so
the pass has the full frame's *pixels* as context, but its *geometry* was collected upstream, during fill.

### 1. The layer's identity is a typed `Resource` — the shared symbol, not a magic string or an index

The handoff has two halves that must not be conflated: a **scene-side identity** (how the user and the instances
name the layer) and a **render-side handle** (how the effect callback reads it, which is unavoidably a lookup in
`RenderSceneBuffers` — the engine's actual store). The fork's magic string was bad because the user typed the
render-side name *by hand on both sides* with no shared symbol. The fix is **not** to eliminate the render-side
name; it is to **derive it automatically from a typed scene-side identity, so nobody ever types a string.**

That identity is a **minimal `Resource` — an identity token, not a policy bag.** Both the opted-in instances and
the producing effect reference the *same resource object*; the engine derives the internal `NTKey` from that
resource's identity and **owns allocation** (keyed by identity → no first-writer-wins aliasing,
`render_scene_buffers_rd.cpp:328-352`; no manual scratch allocation).

```gdscript
class_name CompositorRenderLayer extends Resource

@export var format      : Format = Format.INHERIT_SCENE_COLOR  # enumerated (RGBA8/RGB10A2/RGBA16F/R8/R16UI…) — #7916 set
@export var seed_source : SeedSource = SeedSource.CLEAR        # CLEAR | SCENE_COLOR | a bound Texture — the generalized seed
@export var stage       : CompositorEffect.EffectCallbackType = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
# Structural INVARIANTS, not fields (they define the holdout layer; exposing them as knobs would only shallow it):
#   • depth-test vs the resolved scene depth, depth-WRITE forced off   • size matches the render target
#   • view_count matches the scene (multiview)                         • post-resolve stage ⇒ single-sample
```

`seed_source` is the generalization the fork lacked: the layer is not forced to start from "the main scene after
opaque." It starts from whatever you bind — `CLEAR`, the resolved `SCENE_COLOR`, or an arbitrary `Texture`
(another effect's output, a SubViewport). The effect declares the layers it produces as an **exported property**
(matching the existing `CompositorEffect` house style — `access_resolved_color`, `needs_motion_vectors` … are all
bound properties, and `Compositor.compositor_effects` is itself a `TypedArray` property), and reads by handing back
the **same resource object** — never a string, never a global slot index:

```gdscript
extends CompositorEffect
@export var render_layers : Array[CompositorRenderLayer]              # engine allocates each backing texture, keyed by identity
func _render_callback(stage: int, render_data: RenderData) -> void:
    var tex : RID = get_layer_texture(render_layers[0])              # same resource → no string, no index
    # … composite tex over the frame with the effect's own shader …
```

The only new `GDVIRTUAL` stays `_render_callback` (the existing one); declaration is a property, read is a bound
method callable from the callback — no per-frame GDScript dispatch on the render thread. This is the current
design's typed-target idea **slimmed from a seven-field policy monster to a two-field identity token**: the order
key moves to the instance (§2); depth-source/write, size, and multiview become structural invariants; and the
compositor gains exactly **one** accessor (`get_layer_texture`) — as close to "pure consumer" as possible, since an
effect must have *some* way to name the buffer it reads. *(A scene-`Node` identity variant — opt in by reparenting
under a `RenderLayer3D` — was considered and rejected: a `CompositorEffect` is a `Resource`, so two resources
referencing one resource is clean, whereas an effect referencing a scene node by `NodePath` across the
scene/render boundary is not. An imperative `register_render_layer()` and a `_get_render_layers()` virtual were
also rejected — neither has precedent in this API, which declares needs as properties, not method calls or
virtuals. See interface-design §5.4.)

### 2. An instance opts in by referencing the same layer resource (this IS the opt-in)

```gdscript
# GeometryInstance3D (any material — StandardMaterial3D or ShaderMaterial):
@export var render_layer       : CompositorRenderLayer   # null = render normally; set = held out into this layer
@export var render_layer_order : int = 0                 # caller order key; ties break by stable insertion/fill order
```

Setting `render_layer` holds the instance out of the normal lists and into that layer's deferred pass — the **same
resource object** the effect declares (§1), so the producer↔consumer binding is one editor-checkable symbol, not a
string and not a global index. The editor config-warns if the referenced layer is not declared by any effect in the
environment. `render_layer_order` is an exact `int` (no float32-ULP cliff); **ties break by stable insertion/fill
order**, decoupled from camera depth. The pure comparator is extracted and unit-tested (see Tests).

*(The split binding — assign the `.tres` to instances* and *to the effect — is inherent, not a wart: a producer and
a consumer must agree on an identity, so something is referenced twice. It is the exact shape of `ViewportTexture`,
which binds producer=Viewport and consumer=sample-site and is uncontroversial.)*

### 3. Optional: shader-emitted aux outputs (converges with #7916; not needed for v1)

A material that wants to write more than color into the layer (coverage / weight / ID) opts in with a
`render_mode` and writes `CUSTOM_BUFFER0..N`, exactly as #7916 proposes. **v1 ships without this** — coverage,
where a client needs it, is an engine-written side attachment, so no shader-language change is required yet.

### Engine implementation sketch (Forward+ shown; Mobile mirrors)

- New `RENDER_LIST_COMPOSITOR_LAYER` sibling of `RENDER_LIST_ALPHA` (`render_forward_clustered.h:79-83`); a
  fill-branch routes instances with a non-null `render_layer` into it and out of opaque/alpha.
- The list sorts by `render_layer_order` (stable insertion on ties) — never `sort_by_reverse_depth_and_priority()`.
- At each compositor stage dispatch, before the effects for that stage run, the renderer seeds each layer's
  framebuffer per its `seed_source`, then draws that layer's list into it — depth-test vs resolved scene depth,
  depth-write masked off. The framebuffer is the engine-allocated texture keyed by the layer resource's identity.

## If this enhancement will not be used often, can it be worked around with a few lines of script?

No — but the honest answer names the *two* workarounds and why each fails, because a reviewer will reach for the
second one first.

**Workaround 1 — a `CompositorEffect` compute shader.** Hand-port every participating material to GLSL and keep it
byte-synced forever — hundreds of lines per material, not "a few lines of script." GDExtension cannot bridge it
either: `RendererSceneRender` is a plain C++ class (not `GDCLASS`, no `GDVIRTUAL`), the rendering method is a
hardcoded whitelist (`main.cpp:2440`), and a `CompositorEffect` has no material entry point
(`renderer_scene_render_rd.cpp:298-317`). This is the workaround the GLSL-re-port pain above refers to.

**Workaround 2 — a `SubViewport` (the one a reviewer will suggest).** A SubViewport *does* run the real material,
so it has **no GLSL-re-port cost** — the first argument does not apply to it. It fails for three *structural*
reasons instead, each verified against engine source (`docs/adversarial-review-subviewport.md`):

- **It renders into its own depth buffer**, so held-out geometry cannot be occluded by the main scene. World3D
  sharing transfers only the scenario/object-set (`world_3d.cpp:190`, `viewport.cpp:4907`); depth is
  per-render-target (`renderer_viewport.cpp:287,995`), and the only depth-injection facility
  (`texture_storage.cpp:4540`) is XR-private and unbound. Faking it means duplicating the scene's occluders into
  the SubViewport as depth-only geometry — 2× the geometry, plus color-masking, kept in sync forever.
- **Its transparency is depth-sorted, not caller-ordered** (`render_forward_clustered.cpp:1918`, comparator
  `:722-726`); stock Godot has no depth-decoupled per-instance order key (`sorting_offset` is a depth *bias*,
  `render_priority` a coarse per-material bucket). Faking caller order costs one full viewport render *per layer*.
- **It cannot be seeded by the main viewport's `CompositorEffect` and accumulate in-frame.** Sub-viewports render
  *before* the main viewport (topological order, `renderer_viewport.cpp:98-134,862`), and there is no API to inject
  an externally-authored frame-start seed into a viewport's color attachment (the 3D pass LOADs the internal
  `RB_TEX_COLOR`, `render_scene_buffers_rd.cpp:186`). The data dependency is backwards.

So the residual capability that is reachable **only** from inside the engine is the *intersection*: a held-out
layer occluded against the real scene depth, drawn in caller order, seeded and composited in-frame by the effect
that owns it. That intersection is the primitive below. (Object-isolated post-processing on its own — #7849/#2196
— is broadly SubViewport-reachable and is cited here only as adjacent category heat, not as a capability this
unlocks.)

## Is there a reason this should be core and not an add-on?

Yes — per the above, no add-on can reach it. It requires a new scene render list + deferred pass in the RD
renderers, a typed layer above `RenderSceneBuffersRD`'s named-texture store, and a per-instance property on
`VisualInstance3D` — all core surfaces, none extension-reachable.

### Scope, honesty, and non-goals (so this isn't a god-feature)

- **One small primitive, many clients.** The use-cases above are *clients*, not baked-in features. No orthogonal
  knob explosion.
- **RD-renderers only, capability-gated.** Compositor is Forward+/Mobile only; `gl_compatibility` cannot dispatch
  compositor callbacks (`rasterizer_scene_gles3.h:152`). Registration **hard-fails** (naming the renderer) on
  unsupported renderers/stages — never warn-and-corrupt.
- **MSAA is explicit, not warned.** A layer whose `stage` is post-resolve is single-sample (its geometry is
  composited after the scene resolve); a layer declaring a pre-resolve stage inherits scene MSAA samples. Stated,
  chosen by the author via the `stage` field, never silent.
- **Multiview is a structural invariant, not a knob** — a layer always matches the scene `view_count`; there is no
  field to get wrong (§interface-design 5.6).
- **We do NOT claim** outlines or x-ray — Godot 4.5 ships stencil Outline/X-Ray presets
  ([#7174](https://github.com/godotengine/godot-proposals/issues/7174), PR godot#80710); nor decals/portals/
  planar reflections — #7916's opaque scope. These are cited as category heat, not deliverables.

### Deliberate divergences from #7916 (argued, not accidental)

1. **Opt-in on the instance, not a material directive** — because holdout is a per-instance routing decision, and
   this is what lets any `StandardMaterial3D` mesh participate without a shader.
2. **Identity-resource layers, not indexed buffers** — the layer is addressed by a shared `CompositorRenderLayer`
   object reference (§1), so N independent effects/plugins coexist without colliding on a flat global slot space (a
   limitation #7916 itself carries for its 4 buffers), and the producer↔consumer binding is one editor-checkable
   symbol rather than a hand-typed string or a slot number.

### Anticipated upstream objections (and how this proposal answers them)

The rendering-team concerns this most likely trips, stated plainly so they can be argued rather than discovered:

1. **"This overlaps #7916 — reconcile or wait."** The biggest risk. #7916 owns the custom-buffer/AOV design space
   and is "needs consensus." This proposal is deliberately the **transparent / held-out counterpart** #7916 declared
   out of scope, and it *reuses* #7916's conventions (enumerated formats, the `CUSTOM_BUFFER0..N` aux-output
   vocabulary) rather than inventing rivals — diverging only on identity-vs-index, with an argued reason. **Ask
   before building:** the honest sequencing is to confirm with the #7916 owners whether this belongs *inside* #7916
   or *alongside* it, before any PR. A likely-acceptable outcome is "accepted in principle, landed in #7916's orbit."

2. **"The compositor must not draw meshes."** `CompositorEffect` (#80214) was built consumer-only and DrawList-from-
   effect access was declined (#13405/#13406, closed). This proposal does **not** make the compositor draw geometry:
   the *engine's* scene renderer draws a held-out list into a target; the compositor only **consumes** it
   (`get_layer_texture`). The opt-in is a per-instance scene decision, not a compositor capability. The framing is
   "held-out scene render layer," not "render via the compositor" — and that distinction is load-bearing.

3. **"Scene render now depends on compositor state."** With `render_layers` declared on the effect, a reviewer may
   note the scene renderer's holdout behavior depends on which effects are present. The real driver is the
   **per-instance** `render_layer` reference (the effect only declares the *sink* + allocates it); an instance
   pointing at a layer no active effect declares is a config-warned no-op, not corruption. The dependency is a
   registration-time lookup, not a per-frame coupling.

4. **"Hot-path cost."** A new `RENDER_LIST` + fill-branch in both RD renderers is permanent surface. It is
   capability-gated (RD-only), zero-cost when no instance opts in (an empty list is skipped), and adds no per-frame
   GDScript dispatch (declaration is a property, read is a callback-time method). The touch is one enum slot, one
   fill branch, one sort policy, one draw site per renderer (research §4).

5. **"Needs a champion + proof."** A working spike (the fork's PSX-fold) demonstrates the capability on real
   hardware and proves the `CompositorEffect` wall dissolves. What it still needs is a core-team advocate; this
   proposal exists to earn one, not to pre-empt the PR.

### Tests

The pure ordering logic (stable, total-order, integer-key comparator) is extracted and unit-tested under
`tests/servers/rendering/`, matching the quality bar of the existing sort-seam extraction. An in-tree minimal
repro (one held-out mesh + one effect) demonstrates the feature.
</content>
