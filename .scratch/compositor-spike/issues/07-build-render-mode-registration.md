# [Build · Step 2] Register `compositor_layer` render_mode + shader-variant flag + Mobile gate

Type: task
Status: resolved
Assignee: Aaron Curry
Blocked by: —

## Question

Register the general `compositor_layer` render_mode so membership is a distinct shader variant (the
batch-safety property). No routing yet. Detail: plan § Step 2.

- `servers/rendering/shader_types.cpp` (~`:244`) — `push_back({ PNAME("compositor_layer") })` next to `unshaded`.
- `scene_shader_forward_clustered.h/.cpp` — bool field + `render_mode_flags["compositor_layer"]` bind
  (mirror SPIKE `scene_shader_forward_clustered.cpp:122`, `.h:256`); readable as `surf->shader->compositor_layer`.
- `scene_shader_forward_mobile.*` — bind the flag **only** to `WARN_PRINT_ONCE` "Forward+ only" (mirror SPIKE
  mobile `:123,193-200`).
- **Do NOT** port the spike's coverage-α override or forced depth-write-off here — that is Step 6 policy.

**Done:** a `ShaderMaterial` with `render_mode compositor_layer` compiles on Forward+; a warn fires on Mobile;
no behavioral change yet.

## Answer

**Done (2026-07-31).** Built on `feature/render-to-compositor`. Registers the general
`compositor_layer` render_mode as a distinct shader-variant flag (the batch-safety property), with
no routing yet — exactly per plan § Step 2. Mirrors SPIKE `compositor_fold` verbatim in structure,
renamed to the general `compositor_layer`.

**Files changed (6 engine files, +20/−0):**
- `servers/rendering/shader_types.cpp` — one `push_back({ PNAME("compositor_layer") })` in the SPATIAL
  mode list, right after `unshaded` (global registration → parses on every renderer).
- `renderer_rd/forward_clustered/scene_shader_forward_clustered.h` — `bool compositor_layer = false;`
  field (readable as `surf->shader->compositor_layer`).
- `renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp` — reset in `set_code` +
  `actions.render_mode_flags["compositor_layer"] = &compositor_layer;` bind.
- `renderer_rd/forward_mobile/scene_shader_forward_mobile.h` — same field (Forward+-only; inert on Mobile).
- `renderer_rd/forward_mobile/scene_shader_forward_mobile.cpp` — reset + bind (bound purely to DETECT)
  + a `WARN_PRINT_ONCE("… only supported on the Forward+ renderer …")` gated on `compositor_layer`
  before `depth_draw = DepthDraw(...)`.

**Deltas from the spike:** name only (`compositor_fold` → `compositor_layer`); WARN text generalized
("normal pass"/"composite" rather than "transparent pass"/"fold"). **Did NOT port** the spike's
coverage-α override or forced depth-write-off (`scene_shader_forward_clustered.cpp:350-390` on the
spike) — those are Step 6 policy, per the ticket's explicit exclusion. No routing, no behavioral
change: the flag just parses and is readable.

**Verification.**
1. `scons platform=linuxbsd target=editor dev_build=yes -j24` → exit 0, 00:00:26 (all 3 changed TUs
   compiled, binary linked).
2. Windowed parse-check harness at `/tmp/step2-check/proj` (must be windowed — headless dummy renderer
   never invokes the RD shader compiler, so it can't validate render_mode; see [[spike-fold-run-invocation]]).
   Two shaders per run — a **positive control** (`render_mode compositor_layer`) and a **negative
   control** (`render_mode bogus_mode_xyz_should_fail`), so a clean positive is meaningful only because
   the negative proves the harness actually exercises render-mode validation.
   - **Forward+** (`--rendering-method forward_plus`): `compositor_layer` → **no error**; bogus →
     `SHADER ERROR: Invalid render mode: 'bogus_mode_xyz_should_fail'`. ✅ parses on Forward+.
   - **Mobile** (`--rendering-method mobile`): `compositor_layer` parses (globally registered) **and**
     fires `WARNING: compositor_layer render_mode is only supported on the Forward+ renderer …`; bogus
     still errors. ✅ warn fires on Mobile.

Both halves of the Done bar met: compiles+parses on Forward+; warns on Mobile; no behavioral change.

**Reusable finding (`--rendering-method` override).** A ShaderMaterial's spatial shader compiles
*lazily on first render*, so the parse-check needs the mesh in-tree with a Camera3D and a few frames
before quitting. `--rendering-method mobile` on the same project is enough to exercise the Mobile
scene-shader path and trip the WARN — no separate Mobile project needed. Harness kept at
`/tmp/step2-check/` for reuse by Steps 5/7.

**Not committed** — left as a working-tree change for the user to review/commit, matching the Step 1
pattern.
