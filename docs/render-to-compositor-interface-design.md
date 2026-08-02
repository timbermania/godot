# Interface design — `render-to-compositor-target` (design-it-thrice synthesis)

**Branch:** `feature/render-to-compositor`. **Phase:** design (feeds the godot-proposals submission).
**Inputs:** `render-to-compositor-design-brief.md`, `research-compositor-extension-points.md`. **Method:** three
parallel interface designs under opposed mandates (minimal-depth / type-safety-maximalist / #7916-mirror), then
synthesis. This doc is the recommendation; §5 lists the decisions still open for the `grilling` phase.

## 0. Framing note — center of gravity is ISOLATION, not order (pending demand survey)

This doc originally led with *caller-controlled order* as "the novel core." For **community mass appeal** that is
the wrong headline — order is what the PSX-fold client needs, but the broad, popular demand is for the thing
underneath it: **render *these specific* material-shaded objects into a separate compositor-owned target** (a
holdout / render-layer). Order is one *dimension* within that primitive, not its center. Reframed one-liner:

> **Render selected material-shaded geometry — through its real material pipeline, optionally in a
> caller-controlled order — into a compositor-owned named target, so a `CompositorEffect` can consume it as an
> isolated render layer.**

**Evidence-backed lead use-cases** (from `research-community-demand.md`, exact 👍 via `gh api`). The
neighborhood is one of the hottest in Godot rendering (#7916 193, #7379 136, #644 99, #798 94, #7174 95, #7849
88 — several hundred combined upvotes), but demand is *diffuse* and the shallow end is *already served*, so the
pitch must be disciplined:

- **Lead 1 — the transparent / held-out counterpart to #7916** (193 👍, OPEN). #7916 explicitly declines
  transparent passes and names no holdout/named-target concept — our exact seam. Ride its mindshare; occupy the
  gap it declared out of scope.
- **Lead 2 — object-isolated post-processing** (#7849, 88 👍; #2196, 55 👍). Render marked geometry into a named
  target, run an effect on *just that layer*. This is precisely our named-target + `CompositorEffect` loop.
- **Lead 3 — caller-controlled / NPR / toon transparent draw order** (#3986, 26 👍; #11251 OIT, 17 👍). Our
  per-instance order key + `order_policy` is the first-class answer. (This re-vindicates the *order* dimension as
  a genuine top-3 pitch — just not the sole headline.)

**Do NOT claim (cite only as category heat):** outlines and x-ray — **Godot 4.5 shipped stencil Outline + X-Ray
material presets** (#7174 closed, PR godot#80710 merged 2025-06-11), which pre-empts them; decals/portals/planar
reflections — #7916's *opaque* scope; G-buffer/AOV access (#798), drawable textures (#7379), scriptable
pipelines (#644) — adjacent umbrellas. Over-claiming any of these is the fastest route to closed-as-duplicate.

**Discipline:** breadth belongs in the *pitch* (one seam, many clients), never in the *surface* (no god-feature
knobs). The deferred hold-out seam (§2.5) *is* the render-layer framing — the two reinforce. **Framing mandate:
file as the explicit "transparent / render-layer counterpart of #7916," with NAMED (not indexed) targets and a
caller-controlled order key** — honest, defensible, least-duplicative.

## 1. The three candidates (one line each)

| | Declaration (A) | Typed target (B) | Order key (C) | Signature weak spot (self-reported) |
|---|---|---|---|---|
| **D1 minimal** | `render_mode compositor_target` (bool cap) + `hint_compositor_target` uint selector | `CompositorRenderTarget` resource via new `_get_render_targets()` virtual on the effect | reuse `sorting_offset` (documented overload) | `sorting_offset` dual meaning + bare-uint index-scoping hide an implicit contract |
| **D2 type-safety** | `render_mode renders_to_compositor_target` (bool cap) + instance-side `compositor_target: CompositorRenderTarget` reference | `register_render_target(target) -> RID` handle, validated at registration | first-class `int32 (layer,bias)` per-instance | split binding = two places to touch; `CompositorOrderKey`-as-object over-engineered (should be `Vector2i`) |
| **D3 #7916-mirror** | `render_mode compositor_ordered_pass N` (index directive, mirrors #7916) | `CompositorOrderedPass` resource on the `Compositor`, **indexed**, enum formats | per-instance `ordered_pass_key: int32` + per-pass `OrderPolicy` | **indexed** binding collides across effects (worst multi-effect story); orphaned if #7916 never lands |

## 2. Comparison on the axes that matter upstream

- **Interface depth.** All three are deep on the *target* seam (one declaration hides format resolution,
  collision-checking, capability-gating, framebuffer/LOAD wiring, forced depth-write-off, the new render list).
  They differ on the *declaration*: D1's bare `uint` selector and D3's baked-in shader index are shallower —
  each hides a contract the caller can't see (which effect owns index *N*). D2's checkable instance reference is
  the deepest-honest: the binding is a typed object the editor can validate.
- **Discoverability / type-safety.** D2 > D3 > D1. A `Resource` reference gives an inspector configuration
  warning when the target isn't registered; an `RID` handle makes a misspelling a register-time hard error
  instead of a frame-1 `ERR_FAIL` empty RID. D3's index is type-checked but self-documents nothing. D1's
  `sorting_offset` overload is undiscoverable except via docs — the fork's exact sin.
- **Consistency with #7916.** D3 by construction; but D3 itself concedes the one axis it copied faithfully
  (indexed buffers) is the axis that *hurts* the brief's "N named targets, multiple effects coexisting" goal.
  The right reading: adopt #7916's *conventions that compose* (material self-declares via a `render_mode`
  directive; compositor declares enumerated-format buffers; `CUSTOM_BUFFER0..N` aux-output vocabulary) and
  diverge, with an argued reason, on the *one that doesn't* (flat global index space).
- **GDScript ergonomics.** D1 lightest (reuses channels users know); D3 clean (`render_mode … N`,
  `instance.ordered_pass_key = N`); D2 heaviest (split binding + the object-order-key mistake it self-flags).
  The ergonomic floor is set by *where the binding lives*: on the instance (D2) costs keystrokes but is
  checkable; on a shader uniform (D1) is terse but opaque; in the shader source (D3) is terse but forces one
  shader per target.
- **Cross-renderer / MSAA / multiview honesty.** All three land the same posture — RD-only by construction,
  typed refusal (hard-fail at registration) not warn-and-corrupt, resolved-depth as a declared source. **All
  three flag the identical gap:** the target declaration does not yet carry a typed multiview (`view_count` /
  `array_layers`) or MSAA-samples policy. That field is required before the design is honest under
  multiview/upscaling — an addition, not a guard-rail warning.

## 2.5 The missing facet — SEAM PLACEMENT (deferred hold-out vs interleaved reroute)

All three candidates inherited, unquestioned, the fork's **seam placement**: marked surfaces are *rerouted
mid-pipeline* — pulled from `RENDER_LIST_ALPHA` and drawn at a hardcoded slot wedged between the transparent
draw and the resolve. That interleaving is the design's real awkwardness, and it drives two anti-goals we
otherwise can't shake: the pass slot is *pinned* (can't run after the full frame), and the mental model
("this material renders through the compositor instead of the engine") is backwards — the compositor draws no
meshes.

**Recommended reframe — a deferred, held-out render layer:**
```
render full scene EXCLUDING marked prims      (normal engine render, to completion)
  → render marked prims into the aux target   (self-contained pass, caller order, depth-test vs resolved depth)
  → compositor effect composites aux → scene
  → output
```
The marked instances are **held out** of the scene's normal lists entirely and rendered as a **self-contained
deferred pass** hung off the compositor **stage-dispatch** point (where `_process_compositor_effects` already
runs — research §1/§4), *not* spliced into the transparent draw loop. This:

- **Decouples the primitive from the opaque/transparent internal structure** — much less hot-path surgery, same
  insertion on Forward+ and Mobile.
- **Fixes the framing:** the flag means "hold this instance out and render it, deferred, into a named aux
  target" — a *render-layer / holdout* concept (film-compositing-shaped, legible to the rendering team), not
  "render via the compositor." The engine still shades the material through its real pipeline.
- **Makes the pass slot a first-class declared property of the target** — "after the full engine pass" becomes
  the clean default, not a hardcoded mid-transparent wedge.

**The one honest tradeoff (why `stage` must be a real declared knob, not hardcoded):** deferring past the MSAA
resolve makes the marked geometry **single-sample**. In the existing vocabulary `POST_TRANSPARENT` fires *after*
the resolve (research §4), so "after the full scene render" = post-resolve = no MSAA on the marked prims — fine
for fold/decals/masks. A client that needs MSAA'd marked geometry declares an *earlier* stage (before resolve),
accepting the interleaved placement for that case. So deferred-after-full-render is the **default**; the
interleaved slot survives only as an explicit earlier-`stage` option, chosen by the target's declared stage,
never silent. This converts the fork's single hardcoded slot into a meaningful, honest knob.

## 3. Recommended synthesis — best-of, honoring the convergences

**(A) Declaration — membership is a material `render_mode`; the instance names the layer + order.**
*(DECIDED — see §5.1; reversed from an earlier per-instance-membership draft.)*
Membership lives on the material as `render_mode compositor_layer`, **adopting #7916's rule that pass assignment
stays material-based** so it "remains entirely compatible with GPU driven rendering, where materials are dispatched
instead of geometry" (reduz, #7916 FAQ). A per-instance membership flag would split each material's indirect-draw
batch under GPU-driven dispatch — the exact thing material-based assignment avoids — so it was retracted. The
material still shades through its real forward pipeline (constraint 1); the `render_mode` is a batch-safe permutation
that only re-targets *where* the surface's draw lands. The per-instance surface carries only *which* layer (a
`CompositorRenderLayer` reference) and the draw *order* — routing/scheduling data consumed at cull/fill time, not a
membership decision. Consequence, stated honestly: a `StandardMaterial3D` must become a `ShaderMaterial` to add the
one `render_mode` line. A *second, optional* aux-output `render_mode` survives for shaders that emit
coverage/weight/ID via `CUSTOM_BUFFER0..N` (§3.C / #7916); v1 needs none of that (coverage is engine-written).
Reject D3's index-in-shader and D1's `uint` selector.

**(B) Typed identity — a `CompositorRenderLayer` resource, owned by the effect, validated at registration.**
*(Shape finalized in §5.4: slimmed from D2's 7-field target to a ~2-field identity token.)* A typed `Resource`
carrying enum'd `format`, `seed_source` (`CLEAR` / bound `Texture`; a resolved-`SCENE_COLOR` seed is a named future
extension, gated behind engine-written coverage — not a shipped enum value), and `stage` (reusing
`CompositorEffect.EffectCallbackType`, not a rival vocabulary). **Depth-write, depth-source, size, and multiview
are NOT fields** — structural invariants of a holdout layer (§5.6). The effect declares it as an **exported
`Array[CompositorRenderLayer]` property** (house style — see §5.4), validated at registration: duplicate identity →
hard error, unrepresentable format / unsupported renderer-stage → hard error naming the renderer. The engine
derives the existing `NTKey` from the resource's *identity*, so **no user ever spells a string** and there is no
global slot index. Adopt #7916's *enumerated-format* set; **diverge from its index to a resource identity** for
multi-effect composability — argue this explicitly in the proposal (it is D3's own conceded weak point).

**Enforcement (as built).** The declaration is load-bearing: `CompositorEffect.render_layers` is resolved to a
plain `RenderLayerDeclaration` list and pushed to the render backend at registration, and the held-out pass only
draws a layer some effect on the compositor declares — an instance referencing an *undeclared* layer produces no
target. That misconfiguration is surfaced two ways: the §3(B-binding) editor config-warning on
`GeometryInstance3D` (best-effort — it scans the edited scene's WorldEnvironment/Camera3D compositors, so it warns
only when a compositor is present but does not declare the layer) and, as a runtime backstop for setups the editor
can't resolve, an `ERR_PRINT_ONCE` from the held-out pass. Duplicate identities are diagnosed and dropped from the
pushed set at set-time; an unsupported consume-stage is
refused at the pass and an unrepresentable format at target allocation — both `ERR`-once naming the renderer. On
the Mobile/GLES renderers any declared layer is refused at registration naming the renderer (Forward+-only by
construction). Format/seed/stage live on the declaration (the single source of truth); members carry only
identity + order (§3.C).

**(B-binding) The instance binds the layer by referencing the same resource — this IS the opt-in.**
`GeometryInstance3D.render_layer: CompositorRenderLayer` — a single editor-validatable reference to the *same
object* the effect declares: setting it *is* how an object opts in (no separate capability flag to keep in sync).
Config warning if the referenced layer isn't declared by any effect in the environment. Alongside it,
`GeometryInstance3D.render_layer_order: int` carries the order key (§3.C). Null ⇒ the instance renders normally; a
set layer ⇒ it is held out and rendered into that layer's deferred pass (§2.5). The split binding (instance + effect
both reference the `.tres`) is inherent — the `ViewportTexture` shape — not a wart (§5.4.1).

**(C) Order key — a first-class per-instance `int32`, ties broken by stable insertion.**
Reject reusing `sorting_offset` (D2 & D3 independently; D3's argument is decisive — this design *already* has a
second ordering consumer, so the brief's "reuse until a second consumer bites" default is already met, and the
float32-ULP cliff the fork hit is real). A dedicated `GeometryInstance3D.render_layer_order: int` (exact across
its range; stamped per-instance by the caller; **ties broken by stable insertion/fill order**, documented,
decoupled from camera depth). Keep it a **plain scalar** (or `Vector2i` for layer+bias later) — **not** a
`RefCounted` object (D2's self-corrected mistake). Integer keys make NaN a non-issue — but still **extract the
comparator and unit-test it** under `tests/servers/rendering/` (the `fold_order_sort.h` bar; handoff phase 4).

**Engine-side (hidden):** a new `RENDER_LIST_COMPOSITOR_LAYER` sibling of `RENDER_LIST_ALPHA`
(`render_forward_clustered.h:79-83`), a fill-branch routing held-out instances off the alpha list, the
swap of `sort_by_reverse_depth_and_priority()` for the stable `render_layer_order` sort, and one
`_render_list_with_draw_list` draw into the layer's framebuffer (seeded per `seed_source`) at the declared stage
(research §4). Coverage,
if a client needs it, is a **dedicated engine-written aux attachment** (not a `color.a` override) — the minimal
v1 step of #7916's `CUSTOM_BUFFER0..N` vocabulary, with no shader-language change yet.

## 4. Why this is generalization-by-subtraction (the upstream thesis)

Same caller effort as the fork — one `render_mode`, a per-instance order value, a compositor pass — but: the
mode *names what it does*, the target handshake is *typed and discoverable*, the order key *can't cliff or go
NaN-undefined*, the alpha override is *gone*, and N effects coexist. More capability (decals, ID/mask, NPR
ordered transparency, WBOIT-later) through a smaller, honester interface. PSX-fold is one `.tres` + one shader.

## 5. Decisions (post-grilling — most now closed)

1. **Opt-in surface — DECIDED (reversed): membership is a material `render_mode compositor_layer`**, adopting
   #7916's material-based pass assignment for GPU-driven-dispatch compatibility. The
   `GeometryInstance3D.render_layer` reference is *not* the membership switch — it names which layer the surface
   joins (identity) and pairs with `render_layer_order`; a held-out surface needs **both** the material mode and the
   instance reference. Cost: a `StandardMaterial3D` must be converted to a `ShaderMaterial`. A *separate, optional*
   aux-output `render_mode` is reserved for `CUSTOM_BUFFER0..N` emission only. (The earlier "any material
   participates unchanged / instance-only opt-in" decision was retracted after the #7916 GPU-driven read — see
   Deliberate divergences #1 in the proposal.)
2. **Named vs #7916-indexed — DECIDED: named** (per the framing mandate — named targets let N effects coexist;
   the composability win beats the #7916 index-consistency loss; argue it in the proposal).
3. **Process — DECIDED: file as the explicit "transparent / render-layer counterpart of #7916."** Standalone
   proposal that references #7916 and occupies its declared-out-of-scope seam.
4. **Declaration / identity surface — DECIDED: a minimal identity `Resource`, `CompositorRenderLayer`, declared as
   an exported property on the effect.** The handoff splits into a scene-side *identity* (typed, editor-discoverable)
   and a render-side *handle* (an unavoidable `RenderSceneBuffers` name lookup). The magic string was bad because the
   user typed the render-side name by hand on both sides; the fix is to **derive the render key from a typed identity
   object** so no string is ever typed. Both the instance (`GeometryInstance3D.render_layer`) and the effect
   (`@export var render_layers: Array[CompositorRenderLayer]` + `get_layer_texture(layer)`) reference the *same
   resource object*; the engine allocates the backing texture keyed by that identity (no first-writer-wins aliasing,
   no manual scratch). This is candidate B (typed target) **slimmed from a 7-field policy bag to a ~2-field identity
   token** (format + seed_source + stage); the compositor gains exactly one accessor, staying a near-pure consumer.
   - **4.1 — split binding is inherent, not a wart.** Assigning the `.tres` to instances *and* the effect is the
     same shape as `ViewportTexture` (producer + consumer must agree on one identity). Editor-validated same-object
     reference is the acceptable form.
   - **4.2 — Node identity variant considered and REJECTED.** A `RenderLayer3D` scene node (opt in by reparenting)
     would mirror SubViewport ergonomics, but a `CompositorEffect` is a `Resource`; two resources referencing one
     resource is clean, whereas an effect referencing a scene node by `NodePath` across the scene/render boundary is
     not. Choose the node only if drag-under-it ergonomics ever outweigh effect-side cleanliness — they don't today.
   - **4.3 — declaration is a property, NOT a virtual or an imperative call.** The existing `CompositorEffect`
     declares every need as a bound property (`access_resolved_color`, `needs_motion_vectors`, …), its only
     `GDVIRTUAL` is `_render_callback`, and `Compositor.compositor_effects` is a `TypedArray` property. So
     `render_layers` follows that precedent as an exported `Array[CompositorRenderLayer]`. A `_get_render_layers()`
     virtual (no precedent; risks per-frame GDScript dispatch on the render thread) and an imperative
     `register_render_layer()` (no precedent; the team adds dynamic escape hatches only on demonstrated need) are
     both rejected for v1. `get_layer_texture(layer)` is a bound method callable from inside `_render_callback`,
     which is exactly where frame data is read.
5. **Order key shape — DECIDED: scalar `int render_layer_order`** on the instance, ties broken by stable
   insertion/fill order. `Vector2i(layer, bias)` deferred until a client needs layers. *(low-stakes)*
6. **Multiview / MSAA — DECIDED: structural invariants, NOT declaration fields.** A holdout layer *always* matches
   the scene `view_count` and matches the render-target size; a post-resolve stage is single-sample by construction,
   an earlier stage inherits scene MSAA. These define the primitive, so exposing them as knobs would only shallow
   the interface. They are stated in docs and (where a stage is unrepresentable on a renderer) hard-fail at
   registration — never guard-rail-warned. *(closed)*
7. **Deferred vs interleaved stage — DECIDED: post-resolve is the default.** The layer is a post-resolve,
   pre-compositor-effect pass (full-frame pixels as context; occlusion + a future `SCENE_COLOR` seed both work); an earlier
   (pre-resolve) stage is opt-in only for clients that need MSAA'd layer geometry (§2.5). *(closed)*
8. **Seed source — DECIDED: generalized via `seed_source`** (`CLEAR` | a bound `Texture` in v1; a resolved-`SCENE_COLOR`
   seed is a named future extension gated behind engine-written coverage, not shipped as an enum value), not hardcoded
   to "the main scene after opaque." This fixes the fork's placement bug (which broke mixing transparents between the
   main scene and the layer) and is the stateless-handoff instinct parameterized. *(closed)*
</content>
