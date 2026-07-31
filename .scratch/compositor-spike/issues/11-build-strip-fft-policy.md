# [Build · Step 6] Strip FFT policy; convert guard-rails to invariants/hard-fails

Type: task
Status: open
Blocked by: 10

## Question

Make the engine diff carry zero FFT-specific policy and fail-safe instead of fail-corrupt. Detail: plan § Step 6.

- **DROP** the silent coverage-α ADD/ONE/ONE override, the A2B10G10R10 display-space clamp, and the RGB555
  quantize (SPIKE `scene_shader_forward_clustered.cpp:350-390`). Keep **only** the structural depth-write-off for
  this list.
- Convert the spike's warn-and-**continue** format/size/MSAA/layer guards (SPIKE `:2505-2545`) into **structural
  invariants** (size = render-target size, view_count = scene view_count *by construction*, since the engine now
  allocates the target) or **hard-fails naming the renderer** for genuinely unsupported configs (Mobile, non-RD;
  Open Q5 — hard-fail at registration).
- Coverage, where a client needs it, becomes an explicit engine-written side attachment (Step 7), never a silent
  blend rewrite.

**Done:** engine diff carries zero FFT-specific constants; unsupported configs hard-fail with a renderer-named
error instead of warn-and-corrupt. (Fold-still-renders validated in tickets 03/04 — the FFT game re-adds its
display-space policy in its own Pass A/C effect.)

## Answer

<!-- filled on resolution -->
