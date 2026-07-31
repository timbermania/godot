# [Build · Step 2] Register `compositor_layer` render_mode + shader-variant flag + Mobile gate

Type: task
Status: open
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

<!-- filled on resolution -->
