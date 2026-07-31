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
render full scene EXCLUDING held-out instances     (normal engine render, to completion)
  → render held-out instances into the aux target  (own pass · caller order · depth-test vs resolved scene depth · depth-write OFF)
  → CompositorEffect composites aux target → scene  (at the target's declared stage)
  → output
```

The held-out instances render as a **self-contained deferred pass** hung off the compositor stage-dispatch point,
not spliced into the opaque/transparent draw loop. Default stage is **after the full scene render** (post-resolve);
the target may declare an **earlier** stage when it needs MSAA'd layer geometry (see cross-renderer notes).

### 1. The compositor declares a typed, named target (replaces the untyped `(context,name)` magic string)

A new `Resource` the `CompositorEffect` owns. All policy is typed and validated at registration — no magic
string, no `RD.DataFormat` int leaking to users, no first-writer-wins aliasing
(`render_scene_buffers_rd.cpp:328-352`).

```gdscript
class_name CompositorRenderTarget extends Resource

@export var target_name : StringName                         # unique per compositor; not a handshake string
@export var format      : Format = Format.INHERIT_SCENE_COLOR # enumerated (RGBA8/RGB10A2/RGBA16F/R8/R16UI…) — #7916 set
@export var size_policy  : SizePolicy = SizePolicy.MATCH_RENDER_TARGET
@export var load_policy  : LoadPolicy = LoadPolicy.LOAD        # LOAD (effect seeds, layer accumulates) | CLEAR
@export var stage        : CompositorEffect.EffectCallbackType = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
@export var depth_source  : DepthSource = DepthSource.RESOLVED_SCENE_DEPTH   # test vs scene depth | NONE
@export var order_policy   : OrderPolicy = OrderPolicy.INSERTION   # INSERTION (stable) | ORDER_KEY_ASCENDING
@export var view_policy     : ViewPolicy = ViewPolicy.MATCH_VIEW_COUNT   # multiview: match scene view_count
# depth-WRITE is intentionally NOT a field — forced off for a holdout layer (structural invariant, not a knob).
```

The effect declares its targets (discoverable virtual; an imperative `register_render_target()` escape hatch is
available for dynamic cases):

```gdscript
extends CompositorEffect
func _get_render_targets() -> Array[CompositorRenderTarget]:
    return [preload("res://fold_target.tres")]
func _render_callback(stage: int, render_data: RenderData) -> void:
    var tex : RID = get_render_target_texture(&"psx_fold")   # typed lookup; no magic string
    # … composite tex over the frame with the effect's own shader …
```

### 2. An instance opts in by referencing the target (this IS the opt-in)

```gdscript
# GeometryInstance3D (any material — StandardMaterial3D or ShaderMaterial):
@export var compositor_target : CompositorRenderTarget   # null = render normally; set = held out into this target
@export var compositor_order  : int = 0                  # caller order key when order_policy == ORDER_KEY_ASCENDING
```

Setting `compositor_target` holds the instance out of the normal lists and into the target's deferred pass. The
editor emits a configuration warning if the referenced target is not declared by any effect in the environment.
`compositor_order` is an exact `int` (no float32-ULP cliff); **ties break by stable insertion/fill order**,
decoupled from camera depth. The pure comparator is extracted and unit-tested (see Tests).

### 3. Optional: shader-emitted aux outputs (converges with #7916; not needed for v1)

A material that wants to write more than color into the layer (coverage / weight / ID) opts in with a
`render_mode` and writes `CUSTOM_BUFFER0..N`, exactly as #7916 proposes. **v1 ships without this** — coverage,
where a client needs it, is an engine-written side attachment, so no shader-language change is required yet.

### Engine implementation sketch (Forward+ shown; Mobile mirrors)

- New `RENDER_LIST_COMPOSITOR_LAYER` sibling of `RENDER_LIST_ALPHA` (`render_forward_clustered.h:79-83`); a
  fill-branch routes instances with a non-null `compositor_target` into it and out of opaque/alpha.
- The list sorts by `order_policy` (stable insertion, or by `compositor_order`) — never
  `sort_by_reverse_depth_and_priority()`.
- At each compositor stage dispatch, before the effects for that stage run, the renderer draws any layer lists
  whose target declares that stage, into the target's framebuffer, LOAD, depth-test vs resolved scene depth,
  depth-write masked off.

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
- **MSAA is explicit, not warned.** A target whose `stage` is post-resolve is single-sample (its geometry is
  composited after the scene resolve); a target declaring a pre-resolve stage inherits scene MSAA samples. Stated,
  chosen by the author, never silent.
- **Multiview via `view_policy`** (`MATCH_VIEW_COUNT`) — a typed field, not a guard-rail.
- **We do NOT claim** outlines or x-ray — Godot 4.5 ships stencil Outline/X-Ray presets
  ([#7174](https://github.com/godotengine/godot-proposals/issues/7174), PR godot#80710); nor decals/portals/
  planar reflections — #7916's opaque scope. These are cited as category heat, not deliverables.

### Deliberate divergences from #7916 (argued, not accidental)

1. **Opt-in on the instance, not a material directive** — because holdout is a per-instance routing decision, and
   this is what lets any `StandardMaterial3D` mesh participate without a shader.
2. **Named targets, not indexed buffers** — so N independent effects/plugins coexist without colliding on a flat
   global slot space (a limitation #7916 itself carries for its 4 buffers).

### Tests

The pure ordering logic (stable, total-order, integer-key comparator) is extracted and unit-tested under
`tests/servers/rendering/`, matching the quality bar of the existing sort-seam extraction. An in-tree minimal
repro (one held-out mesh + one effect) demonstrates the feature.
</content>
