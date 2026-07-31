# [Build · Step 5] The held-out pass: seed → draw into engine target on resolved depth → hand off

Type: task
Status: open
Blocked by: 09

## Question

The load-bearing render-loop edit: draw the held-out list into the engine-allocated target, occluded by the
real scene, in caller order. Detail: plan § Step 5 (frame shape ported in structure from SPIKE `:2476-2555`).

- `render_forward_clustered.cpp` at the `POST_TRANSPARENT` dispatch site (`:2459`; pass drawn just before it);
  `_setup_render_pass_uniform_set` for the new list; framebuffer via `FramebufferCacheRD::get_cache_multiview`.
- Per layer partition (Open Q3 — prefer compound-key secondary sort, no per-frame alloc): (a) **seed** the
  engine target per `seed_source` (CLEAR or bound Texture — see Step 7); (b) multiview framebuffer against the
  engine target + the **shared resolved scene `depth_texture`**; (c) draw with `RD::DRAW_DEFAULT_ALL` (**LOAD**,
  no clear), depth-**test** vs resolved scene depth, depth-**write OFF** (enforce structurally, not via material).

**Done:** the held-out mesh renders through its real material into the layer texture, occluded by opaque scene
geometry, in caller order; a trivial `CompositorEffect` reading `get_layer_texture()` composites it back and the
mesh reappears. First end-to-end pixels.

## Answer

<!-- filled on resolution -->
