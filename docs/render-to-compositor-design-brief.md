# Design brief — `render-to-compositor-target` (the upstream generalization of `compositor_fold`)

**Branch:** `feature/render-to-compositor` (fresh off `upstream/master` @ `4e8c061c9b`, stock Godot 4.8-dev).
**Status:** design phase. No engine code yet. Fork branch `spike/compositor-consume-material-output`
(`12e455f119`) is **inspiration only** — not cherry-picked.
**Provenance:** distilled from `docs/compositor-fold-upstream-evaluation.md` (maintainer-lens assessment of the
fork) and the handoff `/tmp/handoff-upstream-compositor-mesh-passes.md`. Where they conflict, the evaluation wins.

## The primitive, in one sentence

> **Draw flagged materials — through their real Godot material pipeline, in a caller-specified order — into an
> auxiliary target that a `CompositorEffect` owns and names**, so an effect can consume material-shaded geometry
> at an arbitrary pipeline stage without hand-porting the material to GLSL.

The fork's PSX "fold" is **one client** of this primitive, not the feature. Decals/stamps, ID/mask/outline
buffers, NPR ordered transparency, impostor/portal capture, and WBOIT-class experiments are the other clients.

## Why it can only live in the engine (the wall it dissolves)

A `CompositorEffect` runs raw `RenderingDevice` passes with a shader **it** supplies; it **cannot invoke a
`ShaderMaterial`**. So anything shaded by a Godot material that a compositor wants to composite must today be
hand-ported to GLSL and kept byte-synced forever. Only the scene renderer can shade a material; only the
compositor owns the auxiliary target. This primitive is the bridge. (Confirmed unreachable by GDExtension /
SubViewport / custom render method in the fork's ADR-0001; the *research phase* re-confirms against upstream.)

## Keep + generalize (the good primitive)

1. **A dedicated render list + side pass** that draws material-shaded meshes into an isolated target through
   full forward shading (`PASS_MODE_COLOR`), so `.gdshaderinc` includes "just work." Generalize beyond one
   hardcoded list.
2. **Compositor-owned target via a named `render_scene_buffers` handoff** — the engine draws into a buffer it
   does not own. Keep the stateless RID handoff; **replace the magic string with a typed target declaration**.
3. **Explicit, caller-controlled draw order, decoupled from depth sort.** Keep the concept; decide the order-key
   type/semantics (see open questions). Reuse `sorting_offset` unless a second consumer justifies a new channel.
4. **LOAD-not-clear** target semantics so an effect can seed the target and the pass accumulates onto it.
5. **Depth-test against resolved scene depth with depth-write forced off** — draw into a side buffer that still
   occludes against the main scene without corrupting it. Make depth-source + write-policy explicit options.
6. **Guard-rail discipline**, but **warn-and-skip on color-correctness violations, never warn-and-corrupt**.
7. **Extraction + unit tests for pure logic** — hold the `fold_order_sort.h` bar (NaN-safe, stable, testable).

## Drop (fork-specific packaging that must not go upstream)

1. The name `compositor_fold` (names the use case). → a mechanism-named `render_mode`.
2. Hardcoded `A2B10G10R10_UNORM` format. → **compositor declares the target format**.
3. Silent coverage-alpha blend override on `color.a`. → **don't overload `color.a`**; a dedicated aux channel
   (v1: engine-written coverage attachment, no shader-language change) or just the material's own blend.
4. 2-bit alpha coverage assumption + binary Pass C gate (use-case coupling).
5. Magic-string handshake (`RB_SCOPE_COMPOSITOR_FOLD`). → **typed target registration/lookup API**.
6. Repurposing `sorting_offset` as an uncapped order key without stating it. → document the overload, or a
   first-class typed order key.
7. Guard-rails hardcoding the FFT combat viewport (LINEAR tonemap / native res / no MSAA). → **define real
   behavior under MSAA / upscaling / multiview**, don't warn.
8. Exactly one hardcoded target/scope. → **N named targets**, multiple effects coexisting.
9. Forward+ only, Mobile warn-only, GLES3 inert. → a **documented capability gate** (RD-only by construction).
10. `is_compositor_fold_supported()` fork-detection shim. → drop; feature exists / is class-ref-discoverable.

## Positioning against proposal #7916 (the pivotal research finding)

`godot-proposals#7916` "Implement a Rendering Compositor" (**OPEN**, "Needs consensus", opened 2023-09-29, VFX/
Techart wishlist) is the closest prior art — and it is a **sibling, not a competitor**. It establishes the
conventions we should adopt *and* explicitly declines the exact case our primitive owns.

**What #7916 proposes** (a compositor-level fixed-pipeline extension):
- A `RendererCompositor` resource that declares **up to 4 custom buffers** in **enumerated formats**
  (`R8…RGBA32F`), set on the compositor centrally.
- **Multiple customizable *opaque* passes** with per-pass action/stencil/depth/clear flags.
- **Material self-declares its target pass** via a shader directive: `compositor_opaque_pass 2;` (default 0).
- Materials read/write those buffers via `hint_custom_bufferN_texture` uniforms and `CUSTOM_BUFFER0 = …`
  fragment outputs (an **enumerated aux-output vocabulary** — exactly the §3.5 idea from the fork evaluation).

**What #7916 explicitly declines** — verbatim:
> "Why opaque passes and not transparent? There is not much of a point in doing this with transparent passes.
> Transparency is sorted from back to front and rendered within a single render pass."

**That decline *is* our primitive.** Our headline — caller-controlled order of (often transparent / side-target)
material-shaded geometry into a compositor-owned LOAD target, depth-tested against resolved scene depth with
depth-write off — is precisely the case #7916 waved off as "not much point." Our motivating clients (PSX fold,
NPR ordered transparency, ordered decals/stamps) are the counter-evidence that there *is* a point.

**Design consequences — adopt the conventions, fill the gap:**
1. **Declaration surface:** align with #7916's shape — a `render_mode`/directive by which a material self-declares
   its target — rather than inventing a divergent one. (Bikeshed the keyword; honor the pattern.)
2. **Typed target:** #7916's "compositor declares N enumerated-format buffers, materials bind by index" is a
   concrete model for our "typed target declaration." Decide: extend #7916's indexed-buffer model, or a
   name-addressed variant over the `NTKey` seam. Reconcile explicitly.
3. **Aux outputs:** #7916's `CUSTOM_BUFFER0..N` is the same enumerated aux-output vocabulary the fork eval
   proposed for coverage/weight/ID. v1 stays minimal (engine-written coverage attachment), but the *language*
   should converge with #7916, not fork from it.
4. **The novel core we own:** the **order key** (caller-controlled, depth-decoupled) and the **transparent/LOAD
   side-target pass**. This is the part with no prior art — where design effort should concentrate.
5. **Proposal framing:** file *with reference to* #7916 — "the caller-ordered / transparent counterpart #7916
   set aside," reusing its buffer + material-directive conventions. Not a fork patch; not a redundant proposal.

## Open design questions (the design + grilling phases must answer)

- **Declaration surface.** How does a material/mesh say "render me into target *X*"? A `render_mode` naming the
  mechanism? A target-name parameter? A `GeometryInstance3D`/`VisualInstance3D` property? What reads cleanly in
  GDScript *and* matches engine `render_mode` conventions (which name capabilities)?
- **Order key.** Type + semantics: per-instance? who stamps it? stable ties? Keep `sorting_offset` (zero new
  API, document the overload) vs. a typed first-class key. Default to reuse until a 2nd consumer bites.
- **Target ownership/registration.** A typed API on `RenderSceneBuffers` / `Compositor` / `CompositorEffect`
  for declaring a named target: name, format, size policy, load policy, and pipeline stage. Multiple targets.
- **Blend + depth policy.** Expose, don't hardcode. Default = material's own blend; depth-source and
  write-policy as options. Coverage → its own channel, not `color.a`.
- **Cross-cutting correctness.** MSAA, 3D upscaling, multiview (`array_layers` / `view_count`): define behavior,
  don't guard-rail-warn.
- **Altitude / process.** The evaluation's verdict: **proposal-first** (`godotengine/godot-proposals`), not a
  drive-by core PR. Confirm no existing proposal covers this (research phase), then decide proposal vs. PR.

## Phase plan (from the handoff)

1. **research** *(running)* — survey upstream compositor/custom-pass extension points + proposal overlap →
   `docs/research-compositor-extension-points.md`.
2. **design-an-interface / codebase-design** — interface shapes for the declaration surface + typed
   target-registration seam.
3. **grilling** — stress-test naming, order-key semantics, cross-renderer scope, MSAA/multiview.
4. **tdd** — keep the pure ordering/registration logic extracted + unit-tested under `tests/servers/rendering/`.
5. **code-review** *(later)* — two-axis review vs `upstream/master` at the **upstream-mergeable** bar.
</content>
</invoke>
