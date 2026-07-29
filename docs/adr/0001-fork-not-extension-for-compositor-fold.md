# Ship `compositor_fold` as an engine fork, not a GDExtension or (yet) upstream

**Status:** accepted

We ship the `compositor_fold` capability as a **custom Godot engine build (fork)**, consumed by the game via
custom export templates. We are *not* delivering it as a GDExtension, and *not* (for now) pursuing an upstream
PR. The near-term goal is to ship the FFT game; the upstream question stays alive only as the long-game exit
from the rebase tax.

## Why a fork is the only option

The capability is **Pass B** — draw the flagged transparent materials *through their real Godot material
pipeline* into a compositor-owned scratch, in caller order, with a coverage-α override. That work can only
happen inside the scene renderer, and there is no extension seam that reaches it:

- **A `CompositorEffect` can't do it.** The callback is handed a `const RenderData*` only (`compositor.h:67-69`,
  dispatch `renderer_scene_render_rd.cpp:298-318`) — no draw list, no material. It can run raw `RenderingDevice`
  draws only with a shader *it* supplies, i.e. a hand-port of the material to GLSL. The exact feature request
  (DrawList access for effects, proposal **#13406**) is CLOSED with no PR. Pass A (seed) and Pass C (resolve)
  already live in the compositor precisely because they *are* shaders we wrote; Pass B can't join them.
- **A SubViewport dodge produces a different image, not the fold.** It runs the real materials but through the
  engine's normal depth-sorted alpha blend — not our caller-ordered, display-space, per-step-UNORM-clamp,
  coverage-accumulating fold — and loses the scene seed and display-space encoding.
- **A custom rendering *method* can't be installed like an extension.** The method name is a hardcoded
  whitelist (`main.cpp:2440`: `forward_plus`/`mobile`/`gl_compatibility`/`dummy`), the concrete renderer is
  `memnew`'d by a string switch (`renderer_compositor_rd.cpp:381-387`), and `RendererSceneRender`
  (`renderer_scene_render.h:41`) is a plain C++ class — **not** `GDCLASS`, no `GDVIRTUAL` — so it is physically
  unsubclassable from a GDExtension. Same wall, one level up.

So the choices are exactly: **custom engine build**, or **hand-port ~940 lines of GLSL kept byte-synced
forever**, or **land it in upstream Godot**. There is no runtime-installable renderer-plugin path.

## Consequences

- **Distribution cost is zero.** A fork is invisible to players — they run a normal exported binary built with
  custom export templates. No "install my Godot" step for anyone.
- **The real cost is the dev/CI rebase tax:** maintaining a ~150-line patched branch and rebasing it onto new
  Godot releases. Small surface, non-zero.
- **Upstream is the only way to shed even that tax** — which is why the proposal question
  ([`../compositor-fold-upstream-evaluation.md`](../compositor-fold-upstream-evaluation.md)) stays alive as a
  future option, not a near-term requirement. If we ever pursue it, the doc's §3.3 re-seam (rename the
  render_mode to the mechanism, typed target declaration, etc.) becomes live work; under fork-and-ship it is
  deliberately deferred as pure cost with no player-visible benefit.
