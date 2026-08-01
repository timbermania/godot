# [Build · Step 5] The held-out pass: seed → draw into engine target on resolved depth → hand off

Type: task
Status: resolved
Blocked by: 09
Assignee: Aaron Curry (claimed 2026-07-31)

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

**Built + verified windowed. First end-to-end pixels.** The held-out pass draws `render_mode
compositor_layer` members — held out of the color pass, grouped by layer identity and caller-ordered —
into their engine-owned target(s), occluded by the real scene, seeded per `seed_source` (CLEAR in v1).

**The spike→plan inversion actually built:** the `compositor_fold` spike was *stateless* (userland
allocated + seeded a scratch, the engine only LOADed into it). This primitive is *engine-allocates-
and-seeds*: the pass calls Step 3's `get_compositor_layer_texture(id, format)` (allocate-on-first-
access) and CLEARs it, rather than warn-and-skip on a missing userland scratch.

**Edits (all in the target tree, `render_forward_clustered.{h,cpp}` + one include):**
1. **`sort_by_layer_order()` → compound** (`.h`): new `SortByLayerThenOrder` comparator — primary key =
   member's layer object id (so each layer's members are **contiguous**, drawable as one element range),
   secondary = `render_layer_order` (the Step-1 rule), ties by submission index (stable-equivalent).
   Replaces Step-4's order-only sort. Q3 resolved: **compound-key sort, walk contiguous runs** (no
   per-frame HashMap).
2. **Repeat-group guard** (`_fill_instance_data`): added `&& inst->render_layer ==
   prev_surface->owner->render_layer` to the instancing-repeat condition so a repeat group never spans a
   layer-partition boundary (line 618 does `i += repeat-1`, which would draw into the wrong target).
   No-op for every other list (their `render_layer` is null).
3. **Fill + uniform set:** added the missing `_fill_instance_data(RENDER_LIST_COMPOSITOR_LAYER, …)`
   (Step 4 sorted but never filled it) + `compositor_layer_rp_uniform_set` built early (reuses the
   transparent-pass UBO), mirroring the spike.
4. **The pass** (before POST_TRANSPARENT, after the transparent resolve): walk contiguous per-layer runs;
   per run resolve the resource, `get_compositor_layer_texture(id, format)`, seed (CLEAR = `DRAW_CLEAR_
   COLOR_ALL` + transparent clear; scene depth LOADed), `FramebufferCacheRD::get_cache_multiview(view,
   layer_tex, depth_tex)`, draw the sub-range via `RenderListParameters` `element_offset=run_start` with
   `COLOR_PASS_FLAG_TRANSPARENT` (**depth-test on / depth-write off structurally** → scene depth never
   corrupted). Static `_compositor_layer_rd_format()` maps Format→RD::DataFormat (INHERIT→
   `get_base_data_format()`).

**Decisions taken this session (grilled):**
- **Format/seed access = ObjectDB lookup at pass time** (A1). ⚠️ **Hardening item for the PR:** this reads
  a main-thread `Resource` on the render thread (data race a Godot reviewer will flag). Pre-PR fix = push
  `format`/`seed_source` down as value enums on the render instance (mirror how `CompositorEffect` copies
  config into RS storage). Deferred to Step 6/polish — not load-bearing for proving the pass.
- **Seed sources this session = CLEAR only.** Bound-Texture / SCENE_COLOR fall back to CLEAR with a
  `WARN_PRINT_ONCE` (never draw over garbage). Bound-Texture is a fast-follow.
- **Proof method = direct RD readback** (not composite-back, which needs Step 7's `get_layer_texture`).

**Verification** (`/tmp/step5-check/proj`, windowed, real Vulkan RTX 5090, Forward+): a GDScript
`CompositorEffect` copies the layer target (via `texture_copy`, on the frame command list — NOT
`texture_get_data` mid-callback, which **deadlocks**) into an owned texture, read back from `_process`.
- **visible** (member full-screen quad, no occluder): `had_texture=true`, layer center = **`255,0,0,255`**
  → member drawn into the engine target.
- **occluded** (opaque wall at z=-3 in front): layer center = **`0,0,0,0`** → member depth-occluded by the
  shared resolved scene depth; CLEAR seed shows through.
Clean exit both; no RD/Vulkan validation errors. (One leaked-Texture warning = the harness's own copy
texture, not engine code.)

**Handoff to Step 6/7:** the accessor path a real effect needs already works generically via
`RenderSceneBuffersRD::get_texture("compositor_layer", str(id))` — Step 7 wraps it as
`get_layer_texture(resource)` + the `render_layers` effect property and does composite-back. Step 6
strips FFT policy / converts guards to invariants **and** should land the format/seed push-down hardening.
Engine build clean (~24s incremental). Uncommitted working-tree change.
