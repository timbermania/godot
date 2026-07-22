# Feasibility: let a `CompositorEffect` consume a transparent `ShaderMaterial`'s shaded output

**Status:** 🟢 Feasibility read complete + **candidate #1 MVP implemented and compiling** (Spike 1,
§8). A least-invasive path that sidesteps both engine walls (R1 and R2). **On-GPU verified 2026-07-22 (§8).**
**Branch:** `spike/compositor-consume-material-output`
**Engine tree:** Godot **4.8-dev** (this checkout; handoff was written vs 4.6 — re-verified against 4.8).
**Owner:** Aaron Curry
**Last updated:** 2026-07-21

> Deliverable for the handoff at `/tmp/handoff-godot-compositor-consume-material-output.md`.
> Companion (blend-*space*) effort: `../godot` @ `spike/forward-plus-display-space-additive`,
> `docs/display-space-additive-blending.md`. **The two efforts converge — see §2.**
> **Agreed architecture → `compositor-fold-design.md`** (this doc is *why/can-we*; that one is *what we're building*).

---

## 0. One-paragraph verdict

The downstream need — "a transparent `ShaderMaterial` shades *itself*, and a compositor consumes the
shaded fragments to fold them in depth order into a clamping buffer" — was framed against two engine
walls: **R1** (a `CompositorEffect` can't invoke a `ShaderMaterial`) and **R2** (materials can't write a
per-pixel A-buffer). Both walls are **still standing in 4.8** and, per live sources, are **years from
falling** (§3). **But the framing over-assumed R2.** The genuinely hard requirement — *N depth-ordered,
interleaved add↔sub fragments per pixel, each clamped per step* — does **not** require a per-pixel
A-buffer. The engine's transparent pass **already** draws mixed-blend-mode materials back-to-front in
depth order; the *only* thing it lacks is a **saturating (UNORM/display-encoded) attachment** — and the
per-step UNORM clamp **is** the fold. **Redirect the flagged transparent materials into a clamping
attachment drawn through their real material pipeline** and the material shades itself (R1 dissolves)
*and* the ordered clamped blend happens in the attachment format (no A-buffer, no SSBO, no atomics —
R2 never enters). This exact mechanism is **already working** in the sibling worktree's Spike 3
(add-only). The recommendation (§4) is to generalize it. **Do not pursue the SSBO/OIT path (candidate #1)
for this need — it is over-engineered, unowned, and stalled.**

---

## 1. The two walls, re-verified against the 4.8 tree

| Wall | Handoff claim (vs 4.6) | Status in **4.8** (this checkout) | Evidence |
|---|---|---|---|
| **R1** — `CompositorEffect` can't invoke a `ShaderMaterial` | true | **Still true.** Effect callback receives `const RenderData*` → `RenderSceneBuffersRD` (it can *read* textures like `get_depth_texture()`), but is handed **no draw list and no material**. It can only issue raw RD compute/draw with a shader *it* supplies. | `scene/resources/compositor.h:67-69` (`GDVIRTUAL2(_render_callback, int, const RenderData *)`); dispatch `renderer_scene_render_rd.cpp:298-318` passes only `RenderData`. Proposal **#13406** (give the effect DrawList access) is **CLOSED / no PR**. |
| **R2** — materials can't write an A-buffer / tagged buffer | true | **Still true.** 4.8 spatial fragment shaders have **no SSBO, no atomics, no `imageStore`, no extra MRT output.** (MRT infra exists but only for `texture_blit`, not `spatial`.) | `shader_language.h:212-247` (DataType has samplers only — no image/buffer types); `shader_types.cpp:137-202` (spatial fragment builtins = ALBEDO/EMISSION/… only); MRT `COLOR0-3` only at `shader_types.cpp:539-542` (`texture_blit`). Atomics/`imageStore` appear **only** in internal `#[compute]` shaders. |

### Callback stages — pass-boundary only, confirmed unchanged in 4.8
`EFFECT_CALLBACK_TYPE_{PRE_OPAQUE, POST_OPAQUE, POST_SKY, PRE_TRANSPARENT, POST_TRANSPARENT}`
(`compositor.h:43-50`). Dispatch order in `render_forward_clustered.cpp::_render_scene`:

```
PRE_OPAQUE (2175) → opaque list → opaque_framebuffer (2225)
POST_OPAQUE (2270) → sky → POST_SKY (2331)
PRE_TRANSPARENT (2409) → RENDER_LIST_ALPHA → alpha_framebuffer (2427)   ← linear-float color + shared depth
   [unconditional MSAA resolve (2439)]
POST_TRANSPARENT (2459)   ← reads already-composited, FLATTENED color
Tonemap / post-process (2568)
```

- **Nothing fires DURING transparent blending** — there is no seam between PRE_ and POST_TRANSPARENT to
  interleave a clamped fold. `POST_TRANSPARENT` sees flattened, already-blended (linear, unclamped) color
  — useless as fold *input* (`access_resolved_color`/`_depth` are force-hidden there because the buffer is
  unconditionally resolved: `compositor.cpp:78-85`).
- The transparent pass **shares the opaque depth buffer** (`get_color_pass_fb`, depth attachment at
  `render_forward_clustered.cpp:198`) → depth-test against opaque scene is free for any pass reusing it.
- **Mobile caveat (downstream ships Mobile):** `POST_OPAQUE`/`POST_SKY` are **unsupported** on the mobile
  renderer (subpass architecture — `render_forward_mobile.cpp:891-894` `WARN_PRINT_ONCE`). `PRE_OPAQUE`,
  `PRE_TRANSPARENT`, `POST_TRANSPARENT` work. Any *renderer-side* pass (the recommendation) is unaffected
  by this — it is not a callback. Note also Mobile **already** tonemaps in-shader into a UNORM attachment,
  so its transparent blend clamps in display space "for free."

---

## 2. The reframing — why R2 is not actually required (the key finding)

The handoff's hard constraint (do not lose this): *"N ordered transparent fragments per pixel with
interleaved opposite blend modes (add↔sub). Only a true per-pixel A-buffer preserves this."*

**This is not true for the FFT prims.** Consider what a per-pixel A-buffer resolve would compute:
sort the pixel's fragments by depth, then fold `dst = clamp01(dst  (op_i)  src_i)` for each fragment `i`
in order, where `op_i ∈ {add, sub}` and `clamp01` is UNORM saturation (the per-step PSX clamp).

The rasterizer already delivers exactly this **without** an A-buffer, provided two things hold:
1. **Draw order = depth order.** The engine's alpha list is sorted back-to-front
   (`render_forward_clustered.cpp:1918` `sort_by_reverse_depth_and_priority`).
2. **Per-fragment op = the material's blend mode**, and each draw does a **read-modify-write against a
   saturating attachment.** The alpha pass *already* binds each material's own blend pipeline (mix/add/sub
   are distinct HW blend states) and draws them in order. The **only** missing ingredient is that
   `alpha_framebuffer`'s color is `R16G16B16A16_SFLOAT` (linear, non-clamping) instead of a UNORM/display
   target. A UNORM store clamps every draw → **per-step clamp, in depth order, across interleaved
   add↔sub — for free.**

> The FFT fold's "group into same-direction runs" step is an artifact of needing one HW blend-op per
> pipeline (you must switch pipeline when the direction flips) — **not** a correctness requirement. The
> engine's transparent draw list switches pipeline per-material for the same reason and produces the
> identical per-prim-clamped result. (Granularity is per-object/per-surface depth sort in both the fold
> and the engine — neither resolves interpenetrating geometry per-fragment. The FFT prims are
> non-interpenetrating quads/sprites, so object-level sort suffices. Shared, acceptable limitation.)

**Consequence:** "let the compositor consume material output" is best satisfied **not** by giving the
compositor a way to run materials (R1) or a per-pixel A-buffer (R2), but by **the engine drawing the
flagged transparent materials — self-shading — into a clamping attachment in depth order.** The compositor
then "consumes" the fold by *reading that attachment* (or the pass targets an effect-owned buffer). This is
**the same primitive** the companion blend-*space* effort already built:

> **Sibling Spike 3 (`../godot`, working, ~7 edit sites, no new files):** a spatial `render_mode
> display_additive` flag → a new `RENDER_LIST_DISPLAY_ADDITIVE` → drawn through the **real forward
> material pipeline** into `[R8G8B8A8_UNORM render target + scene depth]` post-tonemap, with correct
> depth occlusion, lazy pipeline compile, multiview, and an sRGB spec-constant for display-space fidelity.
> Verified numerically stable (frame 2 == frame 40). This *is* candidate #2/#3, de-risked.

So **both handoffs converge on one engine seam**: route a tagged subset of transparent materials into a
dedicated render list drawn (through their own shaders, depth-ordered) into a clamping/display target.
Blend-space wants that target to be the display render buffer; consume-material wants it (optionally)
exposed as a compositor-readable buffer. Same mechanism, two exit points.

---

## 3. Live status of the R2 / OIT ecosystem (checked mid-2026 — volatile, re-check)

| Item | State | Bearing on this task |
|---|---|---|
| **PR #109951** (SSBO/UBO in GDShader) | **OPEN, DRAFT, stalled ~8 mo.** Scoped as a **CPU→GPU read-only feed** (`buffer uniform … ` + `set_shader_buffer`). **No atomics.** Author himself doubts writable SSBO. Not in 4.7/4.8. | **Does NOT unblock R2.** A-buffer needs fragment-side *atomic scatter* — explicitly out of scope. |
| **#6989 / #7516** (buffers/structs to frag shaders) | OPEN, driven only by the stalled PR. | Same. Would give read-only structured input, not writable A-buffer. |
| **#11251** (OIT) | OPEN, **zero PRs**, no consensus (interlock/ROV vs weighted-blended vs compositor). | No engine-native OIT exists or is imminent. |
| **#7916** (custom multi-pass / the Compositor itself) | Compositor API shipped **4.3**; custom multi-pass **writes remain opaque-only**. Transparent-write still unmet. | Confirms candidate #3 (material-tagged secondary output) is *not* a supported path today. |
| **#93217** (transparent objects absent from normal buffer) | OPEN, no PR. | A post-transparent effect can't even see transparent normals — reinforces "read the flattened buffer" is a dead end. |
| **#13406** (DrawList access from inside a `CompositorEffect`) | **CLOSED / archived, no PR.** | The literal "draw from inside the effect" version of candidate #2 is **dead upstream.** |
| **#13919** (transparents unaware of compositor-written depth) | **CLOSED / completed** via a userland post-opaque depth-write technique. | Useful adjacent primitive: a compositor *can* write depth that transparents sort against. |
| **#99493** (depth-texture usage flags) | CLOSED → tracked at **#96737**; resolved by opt-in `TEXTURE_USAGE_STORAGE_BIT`. | Verify the storage/copy bits on the target build if binding depth in compute. |

**Net since 4.6:** the R2 world is essentially frozen. No writable/atomic fragment SSBO, no OIT, no
effect-draws-material path landed or is close. The only motion is one stalled draft that wouldn't unblock
an A-buffer even if merged.

---

## 4. Ranked candidates

Ranked by **(low) implementation cost × (low) invasiveness × (high) completeness of the unblock**.

### ⭐ #1 — RECOMMENDED: Tagged transparent render-list → real material pipeline → clamping/consumable attachment
Generalize the sibling Spike 3 mechanism. Concretely:
- Register a spatial `render_mode` (e.g. `psx_fold` / reuse `display_additive` semantics) — `shader_types.cpp`.
- Flag `ShaderData` from the render_mode; in `_fill_render_list`, route flagged transparent surfaces into a
  new `RENDER_LIST_*` (and **clear it in the per-frame reset block** — the one gotcha the sibling hit).
- Sort back-to-front, `_fill_instance_data`, build the render-pass uniform set while buffers are valid.
- Draw the list via `_render_list_with_draw_list` into a **clamping target**: either (a) the display
  render target (`R8G8B8A8_UNORM`, post-tonemap → display-space clamp, occlusion vs `get_depth_texture()`)
  for an on-screen fold, or (b) an **effect-owned named buffer** (e.g. `A2B10G10R10_UNORM` context buffer
  on `RenderSceneBuffersRD`) that a `POST_TRANSPARENT` `CompositorEffect` reads for further PSX processing.
- Materials with per-prim `blend_add` / `blend_sub` keep their own blend pipelines → interleaved add↔sub is
  handled by draw order + UNORM saturation (§2). No A-buffer.

**Why it wins:** sidesteps **both** R1 and R2 entirely; the material shades itself (zero GLSL
re-implementation — the whole point); **already proven end-to-end** (add-only) in the sibling worktree;
satisfies all three hard constraints (depth-share via shared depth texture; order-sensitive add↔sub via
existing sort + per-material blend + UNORM clamp; clamp lives in the attachment format); ~a dozen in-tree
edit sites, no new shader-language features, no new files strictly required; **convergent with the
blend-space effort** so one primitive serves both asks.
**Risks / open work:** generalize Spike 3 from add-only to **all** transparent blend modes (esp.
`blend_sub`); decide target = render-target vs effect-owned buffer (§6); **Mobile** needs the render_mode
registered (or explicitly rejected) so mobile shader compiles don't choke; **upscaling** (`internal_size !=
target_size`) is architecturally awkward post-tonemap (display color @ target vs depth @ internal — the
sibling guards it off; option A = draw into the tonemap intermediate pre-upscale); MSAA story.

### #2 — Fragment-side SSBO + atomics (unblock R2; #109951 / #11251) — REJECT for this need
The *general* unlock (user-space A-buffer/OIT, no further engine work). **But:** the closest PR is a
stalled draft with **atomics explicitly out of scope**, OIT has no PR/consensus, and delivering writable
atomic fragment SSBO + a linked-list resolve is a **multi-year, cross-cutting, currently unowned** engine
effort. For *this* need it is strictly over-engineered — §2 shows the fold needs no A-buffer. Keep on the
radar only as the "someday, everyone benefits" primitive; **not the path to ship the FFT need.**

### #3 — MRT / tagged secondary output from transparent materials (#7916-extended / #93217) — WEAKER
Let a spatial material write an extra tagged attachment during the transparent pass. Infra exists
(`texture_blit` `COLOR0-3`) but is **not** on spatial, and #7916 is **opaque-write only**. Worse: even if a
material wrote a tagged color, a downstream resolve must still fold the per-pixel fragments **in order** —
which, for interleaved add↔sub, needs… a per-pixel A-buffer again. So MRT alone does **not** solve the
ordering; it reintroduces R2. Dominated by #1.

### #4 — DURING-transparent / post-tonemap callback stage (#13115) — SUBSUMED
A new callback stage doesn't let the compositor invoke materials — it's still a screen-space hook over
flattened color. Only valuable *combined* with #1's geometry pass, and #1 already draws post-tonemap into a
clamping target. #13115 is pre-tonemap/linear anyway. No independent value here.

### #5 — Give `CompositorEffect` DrawList + material access (#13406) — REJECT
Closed upstream, no PR. Even if built, the effect would still supply the shader unless it can bind arbitrary
scene materials/uniform sets — an enormous, fragile API surface duplicating the renderer. #1 gets the same
outcome by letting the *renderer* (which already owns material binding) do the draw.

---

## 5. How #1 satisfies the hard constraints (explicit check)

| Constraint | Satisfied by |
|---|---|
| **Depth sharing without a SubViewport** | The new list's framebuffer binds the scene's `get_depth_texture()` (shared opaque depth). Occlusion vs opaque scene proven in sibling Spike 2/3. |
| **Order-sensitive interleaving (N add↔sub per pixel)** | Existing back-to-front sort + per-material HW blend op + **UNORM saturation per draw** = per-step clamp in depth order. §2. **Not** whole-pass capture. |
| **Clamp in the attachment format** | Target is UNORM (`R8G8B8A8_UNORM` display, or `A2B10G10R10_UNORM` scratch). Saturation *is* the clamp — proven in sibling Spike 1. |

---

## 6. The one decision that gates implementation

Where should the clamped, self-shaded fold **land**?

- **(a) The display render target** — the fold *is* the final on-screen image. This is almost exactly the
  sibling blend-space spike; the "compositor" all but disappears. Simplest. Right if the FFT look is fully
  expressible as "draw these transparents display-space-clamped."
- **(b) An effect-owned `A2B10G10R10_UNORM` context buffer** exposed on `RenderSceneBuffersRD`, which the
  existing `POST_TRANSPARENT` `CompositorEffect` reads and further processes (dither / down-res / palette).
  More plumbing; preserves the project's current compositor ownership and downstream PSX stages.

This is a genuine product decision (does the engine own the final fold, or hand the compositor a
material-shaded input?) and it determines the target-attachment plumbing. **Recommend confirming (a) vs (b)
before writing renderer code** — see the question posed alongside this document.

---

## 7. Sources

- **This checkout (4.8-dev):** `compositor.h:43-69`, `compositor.cpp:75-96`,
  `renderer_scene_render_rd.cpp:298-318`, `render_forward_clustered.cpp` (`_render_scene` 2175/2270/2331/
  2409/2427/2439/2459/2568; `_fill_render_list` 1918; `get_color_pass_fb` 179-198),
  `render_forward_mobile.cpp:891-894`, `shader_language.h:212-247`, `shader_types.cpp:137-202, 539-542`.
- **Sibling worktree** `../godot` @ `spike/forward-plus-display-space-additive`:
  `docs/display-space-additive-blending.md` + the Spike 3 diff (`render_forward_clustered.{cpp,h}`,
  `scene_shader_forward_clustered.{cpp,h}`, `shader_types.cpp`, `scene_forward_clustered*.glsl`).
- **Live GitHub (mid-2026, re-check before relying):** PR #109951 (draft, no atomics); proposals #6989,
  #7516, #7916, #11251, #13115; issues #93217, #96737 (was #99493); closed #13406, #13919.

---

## 8. Spike log

### Spike 1 — candidate #1 MVP, isolated-scratch consume variant (2026-07-21) 🟢 compiles, unverified

**Goal:** prove the *consume* mechanism — engine routes a flagged subset of transparent materials
through their real forward pipeline into an **isolated clamping scratch** that a `CompositorEffect`
reads — rather than the compositor re-implementing each material's fragment shader.

**What was wired (7 in-tree sites, no new files), Forward+ only:**
- `shader_types.cpp` — register `compositor_fold` as a spatial `render_mode`.
- `scene_shader_forward_clustered.{h,cpp}` — `ShaderData::compositor_fold`, set from the render_mode flag.
- `render_forward_clustered.h` — new `RENDER_LIST_COMPOSITOR_FOLD` (+ instance buffer entry).
- `render_forward_clustered.cpp` `_fill_render_list` — flagged transparent surfaces route to the new
  list instead of `RENDER_LIST_ALPHA`; the list is **cleared in the per-frame opaque-fill reset block**
  (the accumulation bug the sibling Spike 3 hit — pre-empted here).
- `render_forward_clustered.cpp` `_render_scene` — sort back-to-front + `_fill_instance_data`; build the
  fold render-pass uniform set while buffers are valid; after the transparent **resolve** and before the
  `POST_TRANSPARENT` callback, draw the list via `_render_list_with_draw_list` into
  `[A2B10G10R10_UNORM scratch + resolved scene depth]` (depth-test on / write off; color cleared to
  black; depth loaded). The scratch is created as a named context buffer `("compositor_fold","color")`.

**Design choices (and current limits):**
- **Isolated scratch, not the display target** (Fork A / option b): the engine does the depth-ordered,
  per-material add/sub fold with UNORM saturation; the `CompositorEffect` consumes the *folded* result.
  It does **not** hand the compositor raw per-fragment data (that would need the walled A-buffer).
- **Internal-size + single-sample**, drawn post-resolve → independent of MSAA and upscaling (sidesteps the
  target-vs-internal depth-size problem the sibling documented for its post-tonemap pass).
- **Linear clamp for now.** The scratch clamps in linear space. Display-space (gamma) per-step clamp — the
  PSX-faithful behavior — is a follow-up: sRGB-encode the fold output via a spec constant, exactly as the
  sibling blend-space Spike 3 does (`linear_to_srgb` + `sc_*` bit). Deferred to keep this MVP focused on
  the *consume* (R1) axis; blend *space* is the companion effort's axis.
- **Cleared to black** each frame → additive materials accumulate correctly; **subtractive** materials
  clamp to 0 from a black base. Faithful `blend_sub` needs the scratch seeded (e.g. copy scene color in)
  — follow-up.

**Verified:** full editor build links clean (`target=editor dev_build=yes`, 2:50). Routing, list
lifecycle, uniform-set timing, and the draw all compile against the 4.8 RD forward-clustered path.
**Not yet verified:** on-GPU visual correctness (occlusion, per-step clamp, add↔sub ordering) — needs a
test scene + run, mirroring the sibling's numeric-probe method. **Not implemented:** mobile renderer
(register or reject the render_mode so mobile shader compiles don't choke); `blend_sub` seeding;
display-space encode.

**Downstream usage:**
```gdshader
shader_type spatial;
render_mode blend_add, compositor_fold, unshaded; // shades itself; no GLSL re-implementation
// ...real fragment logic...
```
```gdscript
func _render_callback(cb_type, render_data):
    if cb_type == EFFECT_CALLBACK_TYPE_POST_TRANSPARENT:
        var rb: RenderSceneBuffersRD = render_data.get_render_scene_buffers()
        if rb.has_texture(&"compositor_fold", &"color"):
            var fold := rb.get_texture(&"compositor_fold", &"color") # folded, clamped, depth-ordered
            # composite / dither / palette over the scene
```

### Spike 1 verification — on-GPU numeric probe (2026-07-22) 🟢 PASS

**Method:** throwaway Forward+ project (`/tmp/spike-fold/proj`, not committed) + a reader
`CompositorEffect` (`fold_probe.gd`) that, in `POST_TRANSPARENT`, reads the `("compositor_fold",
"color")` scratch via `RenderingDevice.texture_get_data`, decodes the center pixel of the
`A2B10G10R10_UNORM` buffer, and logs RGB at frame 2 and frame 40. Ran headed on the RTX 5090
(Vulkan 1.4, Forward+), one scene per criterion. No Vulkan validation errors/warnings/leaks.

| Scene | Setup | Predicted | Observed (R) | ✓ |
|---|---|---|---|---|
| `control_add` | 1× `blend_add` 0.4, from black | 0.40 | **0.3998** | depth-ordered fold from black works |
| `occlusion` | `blend_add` 0.4 quad **behind opaque wall** | 0.00 | **0.0000** | depth-test vs shared scene depth culls it |
| `clamp2` | 2× `blend_add` 0.4 | 0.80 | **0.7996** | additive accumulates below saturation |
| `clamp5` | 5× `blend_add` 0.4 (Σ=2.0) | 1.00 | **1.0000** | UNORM per-step clamp saturates at 1.0 |
| `order_a` | add 0.6 (back), sub 0.3 (front) | 0.30 | **0.3001** | back-to-front: +0.6 then −0.3 |
| `order_b` | sub 0.3 (back), add 0.6 (front) | 0.60 | **0.6002** | back-to-front: (0−0.3→**clamp 0**) then +0.6 |
| `temporal` | static `clamp5`, frame 2 vs 40 | equal | **1.0000 == 1.0000** | per-frame clear holds; no accumulation |
| `control_normal` | unflagged `blend_add` quad | no scratch | **no fold texture created** | routing is selective, not all-transparents |

**Key result — `order_a` (0.30) ≠ `order_b` (0.60)** with the *same two quads* at swapped depths
is the decisive proof: a signed accumulate-then-clamp would give 0.30 for **both**. The 2× difference
is only explicable by **per-step UNORM clamp applied in depth order** — so criteria "per-step clamp"
and "depth-ordered add↔sub" are confirmed *together*. All three §5 hard constraints hold on real
hardware. Known limits behaved as documented (§8): sub-from-black clamps to 0 (`order_b` back step);
linear-space clamp; Forward+ only.

### Spike 2 — ordered-array submission: fold in caller order, not depth (2026-07-22) 🟢 PASS

**Goal:** prove the refined submission model (design work-item #2) — the engine folds flagged prims in
the **caller's** supplied order, never by camera depth — with the smallest possible diff.

**What changed (Forward+):** deleted the fold list's `sort_by_reverse_depth_and_priority()` and replaced
it with a new **priority-only** `sort_by_priority()` (a 6-line `SortByPriority` comparator + one changed
call site in `_render_scene`). The caller conveys fold order via the material's **existing
`render_priority`** field (`sort.priority`, already populated at `render_forward_clustered.cpp:4324` and
already read by the alpha sort). No new API, no new instance data — **net touch-point delta is negative**
vs. Spike 1, and it mirrors on mobile as a one-line sort swap. Occlusion is untouched (it's the separate
depth-attachment test, not the sort). Within-run order rides MultiMesh instance-buffer position (a
MultiMesh is one render-list element drawn with N hardware instances); cross-run order is the unique
per-run `render_priority`, so sort stability is moot.

**On-GPU numeric probe** (same method/harness as Spike 1, RTX 5090, Forward+):

| Scene | Setup | Predicted (R) | Observed (R) | ✓ |
|---|---|---|---|---|
| `prio_add_last` | sub(0.3) p1 + add(0.6) p2, **same depth** | 0.60 | **0.6002** | add folds last by priority (from-black sub clamps to 0) |
| `prio_sub_last` | add(0.6) p1 + sub(0.3) p2, **same depth** | 0.30 | **0.3001** | *same two quads*, swapped priority → 0.6 then −0.3 |
| `prio_beats_depth` | add(0.6) p1 **front** + sub(0.3) p2 **back** | 0.30 | **0.3001** | priority order **overrides** depth order |

**Decisive results:** (1) `prio_add_last` (0.60) ≠ `prio_sub_last` (0.30) with the *same two quads at the
same depth* — depth cannot distinguish them, so only caller priority explains the 2× swing. (2)
`prio_beats_depth` = 0.30, not the 0.60 a back-to-front depth sort would produce (sub-back→clamp 0, then
add-front→0.6) — the engine drew add-first/sub-last per priority, **ignoring depth entirely.** Frame 2 ==
frame 40 (temporal stable); no validation errors. The ordered-array submission model holds on hardware
with a smaller diff than the scene-tree-routing spike.

### Spike 3 — real MultiMesh transport: per-instance prim data + run ordering (2026-07-22) 🟢 PASS

**Goal:** close design item #5 — prove the fold works with the *actual* transport the FFT game uses
(one MultiMesh whose instance buffer **is** the ordered prim array, per-prim data in per-instance custom
data), not the `MeshInstance3D` stand-ins Spike 2 used, and that it composes with cross-run priority order.

**No engine change** — GDScript/shader only. A flagged `MultiMesh` (`use_custom_data = true`) carries one
prim per instance; the material reads `INSTANCE_CUSTOM.rgb` in `vertex()` and forwards it to `fragment()`
via a varying (INSTANCE_CUSTOM is a vertex-stage builtin — reading it directly in fragment fails to
compile). Runs are separate MultiMeshes with distinct `render_priority`.

| Scene | Setup | Predicted (R) | Observed (R) | ✓ |
|---|---|---|---|---|
| `mm_add_last` | sub-run(0.3) p1 + add-run(**2 insts** 0.3+0.3) p2, same depth | 0.60 | **0.6002** | per-instance data sums within run; add folds last |
| `mm_sub_last` | add-run p1 + sub-run p2 (swapped priority) | 0.30 | **0.3001** | same two runs, priority-swapped |

**Proves three things together:** (1) a flagged MultiMesh **routes into the fold** and creates the scratch;
(2) **per-instance `INSTANCE_CUSTOM` data flows through** — the add-run's two instances each carrying 0.3
summed to 0.6, so per-prim data is read per instance and shaded through the real material; (3) **cross-run
`render_priority` order holds with real MultiMeshes** (0.60 ≠ 0.30 on swap). This is the FFT transport
end-to-end (instance buffer = ordered prim array, per-instance custom data, priority = run order). The only
remaining unknown is the game-side *staging* API (writing the 24-float records), not the engine fold path.

---

## Change log
- **2026-07-22** — Spike 3 **verified on-GPU** (Forward+): closed design item #5 — the fold works with the
  real MultiMesh transport (instance buffer = ordered prim array, per-prim color in `INSTANCE_CUSTOM` via a
  varying) composed with cross-run `render_priority` ordering. No engine change; GDScript/shader only.
- **2026-07-22** — Spike 2 **verified on-GPU** (Forward+): ordered-array submission — the fold list now
  sorts by caller `render_priority` only (`sort_by_priority()` replaces `sort_by_reverse_depth_and_priority()`),
  so the engine folds in the game's submitted order, not camera depth. Probe across 3 scenes proved
  priority controls the fold and overrides depth (`prio_beats_depth` = 0.30 ≠ depth-sort's 0.60). Net
  touch-point delta negative; no new API. Mobile port priced as a near-clean mirror (design §6).
- **2026-07-22** — Spike 1 **verified on-GPU** (RTX 5090, Forward+). Numeric center-pixel readback of
  the `("compositor_fold","color")` scratch across 8 scenes: depth occlusion, per-step UNORM clamp
  (saturates at 1.0), depth-ordered add↔sub (order_a 0.30 ≠ order_b 0.60 — the per-step-clamp
  signature), temporal stability (frame 2 == frame 40), and selective routing (unflagged materials
  don't create the scratch). All predictions matched; no validation errors. Status → verified. See §8.
- **2026-07-21** — Spike 1: implemented candidate #1 (isolated-scratch consume variant) in the Forward+
  RD renderer; 7 edit sites, no new files; full editor build links clean. Visual verification, mobile,
  `blend_sub` seeding, and display-space encode are documented follow-ups. See §8.
- **2026-07-21** — Initial feasibility read. Verified R1/R2 still stand in 4.8; pulled live PR/proposal
  status; mapped compositor dispatch + transparent-list build; **reframed the problem** (R2/A-buffer not
  required — §2) after finding the sibling worktree's Spike 3 already proves the render-list-into-clamping-
  attachment mechanism. Recommended candidate #1; rejected the SSBO/OIT path for this need.
