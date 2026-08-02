# [Upstream-hardening] Make `CompositorEffect.render_layers` load-bearing (validated at registration)

Type: task
Status: resolved
Blocked by: —
Assignee: Aaron Curry (claimed 2026-08-02)

## Why (scope note)

This ticket **reopens engine work beyond the original spike scope** ("proof-in-hand"). Directed by the user
on 2026-08-02 as **upstream-PR prep**: the proposal (`interface-design §3B`) sells `render_layers` as
*"a typed identity, owned by the effect, validated at registration: duplicate identity → hard error,
unrepresentable format / unsupported renderer-stage → hard error naming the renderer."* — but the shipped
code (ticket 12) made `render_layers` a **purely declarative, inert** array: it is stored in
`CompositorEffect` and referenced **nowhere** outside `scene/resources/compositor.cpp` (grep-confirmed). The
held-out pass is driven entirely by the per-instance `GeometryInstance3D.render_layer`, and
`get_layer_texture()` deliberately resolves by identity **without** consulting `render_layers`
(`compositor.cpp:218`). So doc and code disagree on whether `render_layers` is load-bearing — the first thing
an upstream reviewer will catch. **Decision (2026-08-02): make it load-bearing** (vs. drop it / document the
gap), because "typed refusal at registration + producer↔consumer agree on one identity (the `ViewportTexture`
shape)" is the differentiator vs. the fork's magic-string/guard-rail approach that the whole #7916 pitch rests on.

## Target data path

**Declaration (effect side).** `CompositorEffect::set_render_layers()` resolves each `CompositorRenderLayer`
to a plain descriptor `{ ObjectID identity, Format format, SeedSource seed_source, EffectCallbackType stage,
RID seed_texture }` and **pushes it to the RenderingServer** (mirroring `set_access_resolved_color` et al.,
which push to `rs->compositor_effect_set_*`). The stored `TypedArray` stays verbatim (editor stays editable);
the *pushed* descriptor list is the load-bearing artifact.

**Registration validation.**
- *Setter-side, renderer-independent* — reject `null` entries; **duplicate identity → hard error**
  (`ERR_PRINT`-and-skip the dupe when building the push list; array stays editable, dupe never reaches RS).
- *Renderer-side, names the renderer* — **unrepresentable format** / **unsupported stage** →
  `ERR_PRINT_ONCE` naming the active renderer, and the layer is refused (no target). Forward Mobile / GLES3:
  any declared `render_layers` → refuse at registration naming the renderer (complements the existing
  shader-side gate at `scene_shader_forward_mobile.cpp:200`).

**Load-bearing payoff — the declaration gates the mechanism.** The renderer builds an
`ObjectID identity → declaration` map from the effects on the current compositor. The held-out pass, grouping
instances by `render_layer.layer_id`, looks the identity up: **declared →** allocate/seed the target from the
*declaration's* format/seed/stage (single source of truth); **undeclared →** the instance is **not** held out
into an orphan target (skip; editor config-warning on the instance). No declaration ⇒ no target = genuinely
load-bearing.

## Commit sequence (project style: tiny commits)

1. **Setter-side hard validation** — `set_render_layers`: reject `null`, `ERR_PRINT` + skip duplicate
   identity. Pure, renderer-independent, no-regrets. *(landing first, this session)*
2. **RS plumbing** — `RenderLayerDeclaration` plain struct; `compositor_effect_set_render_layers` on
   `rendering_server.{h,cpp}` + `renderer_scene_cull` PASS macro + `RendererCompositorStorage`
   (`compositor_storage.{h,cpp}`) storage field + getter. `set_render_layers` builds + pushes descriptors.
   Compiles; still inert on the render side (stored, not yet consumed).
3. **Renderer consults the declaration** — build the identity→declaration map per frame; in the held-out
   pass skip undeclared layers, take format/seed/stage from the declaration, `ERR_PRINT_ONCE` (naming the
   renderer) on unrepresentable-format / unsupported-stage. **This is the load-bearing flip.**
4. **(Optional, flagged) single-source-of-truth cleanup** — slim `RenderLayerMembership` to `{identity,
   order}`; pull format/seed from the declaration. Removes the current instance-side duplication of
   format/seed. Separable, higher risk to the working held-out pass — do last or defer.
5. **Mobile/GLES registration refusal** — forward-mobile refuses declared `render_layers` at registration,
   naming the renderer.
6. **Tests + doc reconciliation** — unit test: duplicate-identity rejection + undeclared-layer gating;
   update `interface-design §3B` and the class-ref XML to state the enforced-at-registration +
   declaration-gates-target semantics exactly; resolve this ticket + update `map.md`.

**Done:** `render_layers` is authoritative — a declared layer with a bad format/stage fails loudly at
registration naming the renderer; an instance referencing an undeclared layer renders no orphan target; the
proposal and the code tell one story. The captured FFT proof still renders (regression-checked windowed).

## Answer

**RESOLVED 2026-08-02.** All six commits landed and each built green (`linuxbsd/editor/dev`, build-after-each);
the compositor unit suite passes (11 cases / 31 assertions, incl. the new
`[CompositorEffect] render_layers … tolerate duplicate identities`). Working tree is **uncommitted** — the six
steps are staged as edits, not yet `git commit`ed.

- **C1** setter-side duplicate-identity `ERR` (`scene/resources/compositor.cpp`).
- **C2** RS plumbing: `RenderLayerDeclaration` (`render_layer_membership.h`) + `compositor_effect_set_render_layers`
  through `rendering_server.{h}` / `rendering_method.h` / `rendering_server_default.h` / `renderer_scene_cull.h`
  / `renderer_scene_render.{h,cpp}` / `storage/compositor_storage.{h,cpp}`; `set_render_layers` builds + pushes.
- **C3** load-bearing flip: the held-out pass builds an `identity → declaration` map from the compositor's
  effects, skips undeclared layers, and refuses an unsupported stage `ERR_PRINT_ONCE` naming the renderer
  (`render_forward_clustered.cpp`).
- **C4** single source of truth: `RenderLayerMembership` slimmed to `{layer_id, order}`; the pass reads
  format/seed from the declaration; `GeometryInstance3D::_update_render_layer` stops resolving them and drops
  its per-layer `changed` connection; `CompositorEffect` now connects to each declared layer's `changed` and
  re-pushes (live edits preserved); the enum-mirror `static_assert`s moved to `compositor.cpp` (the cast site).
- **C5** Mobile registration refusal naming the renderer (`render_forward_mobile.cpp`), complementing the
  existing shader-side `compositor_layer` gate.
- **C6** test (above) + doc reconciliation: `interface-design §3B` "Enforcement (as built)" note;
  `doc/classes/CompositorEffect.xml` `render_layers` / `get_layer_texture` rewritten (were documented as an
  inert hint — now authoritative).

**Deferred follow-up (one item):** the §3(B-binding) *editor config-warning* on `GeometryInstance3D` when its
`render_layer` is set but declared by no effect in the environment. Correctness is already handled (the renderer
produces no orphan target); this is editor ergonomics and needs the instance to resolve the active
environment→compositor→effects at edit time. Noted in the §3B "Enforcement (as built)" doc note. **Verification
gap:** the windowed FFT proof was NOT re-run this session — recommended before opening the PR (per
`compositor-fold-game-run-invocation`).
