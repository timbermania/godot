# Plan: engine-shaded, display-space, ordered fold (into a compositor-shared scratch)

**Status:** 🟢 Design converged + **all four §7c probes resolved AND engine Pass B built & verified on
hardware (2026-07-22 → -23, RTX 5090 / Vulkan 1.4, Forward+).** GATE CLEARED (§7c.1). §7c.2/.3 pass. §7c.4
surfaced a **correction** to §7a.5's ordering mechanism; owner chose **uncapped order via an engine change**
(§7a.5 resolved). **Engine Pass B is now implemented and empirically verified** — coverage-α override,
compositor-owned scratch-RID handoff (§7a.2), uncapped `sorting_offset` ordering, and guard-rail asserts. See
**§10 (built + verified)**. Owner-decisions resolved in the grill (§7a); converged through grounded reading of
the *shipped* FFT compositor. **Not yet committed** (working-tree engine diff + spike harness). **Supersedes** the "engine owns the fold, from black, in
linear" framing in the two companion docs (`compositor-fold-design.md`,
`compositor-consume-material-output-feasibility.md`), which now carry errata pointing here.
**Owner:** Aaron Curry · **Engine tree:** Godot 4.8-dev · **Date:** 2026-07-22

---

## 1. The fixed requirements (do not violate)

1. **No new blend modes in the engine** — use only the existing `blend_add` / `blend_sub` / `blend_mix`.
2. **No `gdshader`/`gdshaderinc` → GLSL duplication** — prims (especially units) shade through their
   real Godot materials, never a hand-port.
3. **Tight blending-order control** — the fold honors an **age-within-depth-bucket** order
   (`OTDepthPrimOrder`), not camera depth.

Everything below is chosen to satisfy all three simultaneously.

---

## 2. What the real system is (grounded in the shipped code, read 2026-07-22)

- **The consumer — `CombatDisplaySpaceComposite.gd`** does a **3-pass, scene-seeded, DISPLAY-space** fold:
  - **Pass A (seed):** opaque scene color → owned `A2B10G10R10_UNORM` scratch, **linear→display**, coverage α=0.
  - **Pass B (fold):** hardware-blend the depth-ordered *runs* onto the scratch (`add`/`sub`/`mix`;
    UNORM saturation = per-prim clamp; depth-tested vs the opaque scene). Pass count = #runs, not #prims.
  - **Pass C (out):** scratch → scene, **display→linear + RGB555 quantize**, discarding untouched
    (coverage==0) pixels so the 3D background stays pristine.
- **The order — `OTDepthPrimOrder.order()`** is an LSD radix of two stable counting sorts: **depth-bucket
  far→near** (primary, 0.19-unit buckets) then **age newest-on-top** (secondary). The age tie-break is
  the **DEMI2 / E046 fix** — it keeps the white additive core over the black subtractive prim where they
  tie in a bucket. Emits maximal same-**direction** runs (add {1,3} / sub {2} / mix solo).
- **Why the engine is involved at all (the real R1 pain).** Units are drawn by
  `unit.gdshader` → `unit_sprite_body.gdshaderinc` — **939 lines** plus nested PSX includes
  (`psx_ot_depth`, `psx_color_stack`, `psx_par`, palette, dither): up to **10 composited tiles × 3 layers**
  (body / weapon / effect), each tile with atlas rect, screen loc, H/V flip, rotation-around-pivot, palette;
  plus billboarding, shadow, PAR correction, OT depth. **Rendering a unit through the compositor means
  hand-porting all of that to raw GLSL and keeping it byte-synced forever** — the R1 wall (a
  `CompositorEffect` can't invoke a `ShaderMaterial`). That is the whole motivation.
- **The prims emit RAW display texels.** The unit/particle materials are `unshaded` and declare **no
  `source_color`**, so Godot never linearizes the texture sample. The fold fragment says it outright:
  *"the sprite texel is raw sRGB/display … the contribution stays in DISPLAY space; the blend adds it to
  the display-space scratch."*

---

## 3. The load-bearing insight

**The hardware blender is color-space-agnostic.** `BLEND_OP_ADD` computes `dst + src` on the **raw numbers**
in the attachment — it has no notion of linear vs gamma. A blend is a **gamma-space** blend *iff both
operands hold gamma-encoded numbers*, **regardless of who issues the draw.**

So the compositor's fold is gamma-space not because the compositor is special, but because (a) its scratch
is **display-seeded** (Pass A) and (b) its prims emit **raw display texels**. Therefore **the engine drawing
those same raw-texel materials into that same display scratch, with the same hardware `ADD`, produces the
identical gamma fold.** The color space lives in the *data*, not the blender. This is what lets the engine
take over the *shading* (killing R1) without moving the blend into linear space.

---

## 4. The architecture

The engine draws the flagged, **real-material** prims (which emit raw display texels) with their **existing**
blend modes, in **`OTDepthPrimOrder` order**, **blending as they are drawn** directly into a **display-space
scratch shared with the compositor**.

> **Blend-as-drawn, not shade-then-blend.** There is no intermediate "shade all prims flat to a buffer"
> step — that would collapse overlapping prims to the topmost per pixel (the A-buffer wall, R2, walled).
> Blending each prim as it's drawn needs **zero** per-pixel storage — exactly how the rasterizer's normal
> transparent pass already works.

```
 opaque scene (units are depth_draw_opaque → free)         OTDepthPrimOrder (CPU): depth-bucket + age
        │                                                          │  → ordered runs + per-run order key
        ▼                                                          ▼
 ┌───────────────┐   A: linear→display   ┌────────────────────────────────────────────┐
 │ opaque scene  │──────────────────────▶│  DISPLAY-space scratch (A2B10G10R10_UNORM)   │
 └───────────────┘   (seed, coverage=0)  │                                              │
                                          │  B (ENGINE, NEW): draw real-material prims   │
   render_priority = run index  ─────────▶│     (unit.gdshader etc., raw display texels),│
   existing blend_add/sub/mix             │     existing blend, IN ORDER, blend-as-drawn,│
   depth-tested vs opaque scene           │     depth-tested → gamma fold, per-prim clamp│
                                          └───────────────────────┬──────────────────────┘
                                            C: display→linear + RGB555 quantize + coverage discard
                                                                  ▼
                                                          final scene color
```

**Pass ownership (locked — thin compositor, §7a.1):**
- **Pass A (seed)** — copy the free opaque scene into the compositor-owned scratch as display, coverage α=0.
  *Compositor,* at PRE_TRANSPARENT.
- **Pass B (fold)** — **the new engine capability:** draw real-material prims into the display scratch,
  existing `add`/`sub`/`mix`, in **submission order** (no depth re-sort), depth-tested, alpha forced to
  `ADD/ONE/ONE` for coverage. *Engine,* during the transparent pass.
- **Pass C (out)** — dither / palette / RGB555 quantize / coverage discard. *Compositor,* at POST_TRANSPARENT.

**Renderer: Forward+, not mobile.** Mobile blends transparents in **linear** too (verified —
`scene_forward_mobile.glsl:2354-2361` outputs `out_color / luminance_multiplier`, no encode; tonemap is a
later pass), and that `÷ luminance_multiplier` would **corrupt** raw display values. Forward+ applies **no**
fragment-output color conversion by default (that's why the sibling had to *add* a `linear_to_srgb`
spec-constant for *its* linear materials) — so raw display texels pass straight through. **No sRGB encode is
needed here at all**, because the materials already emit display values.

---

## 5. Why it meets all three requirements

| Req | How |
|---|---|
| **1 — no new blend modes** | Existing hardware `add`/`sub`/`mix`. The engine *issuing* the blend is not a new mode; the gamma-ness is in the data (display scratch + display texels). |
| **2 — no shader duplication** | Prims shade through `unit.gdshader` / `unit_additive.gdshader` etc. — the engine draws the real material. Nothing re-ported to GLSL. |
| **3 — order control** | The engine draws in `OTDepthPrimOrder` order via `render_priority` = the run's order index (Spike 2's `sort_by_priority()`). Depth is *not* the sort key. |

---

## 6. What the compositor keeps vs. sheds

- **Keeps:** the CPU staging + `OTDepthPrimOrder` sort; Pass A (seed) if it stays compositor-side; Pass C
  (dither / palette / RGB555 quantize / coverage discard).
- **Sheds:** re-implementing prim shading in raw GLSL (the old Pass-B fragment) — that becomes the engine's
  real-material draw. This is the entire point.

---

## 7. Decisions + remaining work

The design questions that were Aaron-owned were resolved in a grounded grill (2026-07-22); what remains is
engine-facts to read and empirical probes to run. This section is the source of truth for both.

### 7a. Decisions locked (do not re-litigate)

1. **Seam — thin compositor.** The engine adds **only Pass B** (the fold). Pass A (seed) and Pass C (out)
   stay as the shipped userland shaders. Smallest engine patch; the color-space encode/decode stays where
   it's iterated. *Cost:* one shared-buffer handoff across the seam (see 7a.2).
2. **Scratch ownership (was §7.8) — compositor-owned.** The compositor allocates and owns the
   `A2B10G10R10_UNORM` scratch (as it does today), seeds it in Pass A, and **hands the RID to the engine's
   fold**. The engine's Pass B contract is stateless: "draw the flagged prims, in submission order, into
   this RID." Buffer lifecycle/resize stays out of the engine patch.
3. **Tonemap (was §7.6) — Linear required, as a hard precondition.** The display-space add is only correct
   under `TONE_MAPPER_LINEAR` (ACES/AgX/Reinhard corrupt it). Every FFT combat probe already sets Linear,
   so this ratifies existing convention. **Guard-rail:** the fold asserts/warns if the combat viewport has a
   non-Linear tonemapper.
4. **MSAA + upscaling — documented non-goals (v1).** No MSAA and no 3D upscaling in FFT combat — grounded
   in `project.godot` having **no `scaling_3d` and no `msaa` keys** (→ engine defaults: scale 1.0 so
   `internal_size == target_size`, MSAA disabled). *Note:* the `window/stretch/mode="viewport"` line is 2D
   window presentation and irrelevant — the compositor works on render buffers at `internal_size` before
   that stretch. **Guard-rail:** assert `internal_size == target_size` and MSAA off; if a combat
   `SubViewport` ever sets `scaling_3d_scale != 1.0`, the fold is undefined until revisited.
5. **Fold order (was §7.5) — no depth sort; CPU owns order.** The CPU-side `OTDepthPrimOrder` already emits
   runs in exact fold order; the engine draws the routed list **with no camera-depth re-sort** (confirmed
   §7c.4 — `sort_by_priority()` has no depth term). *Requirement 3 (order control) is met: depth is never the
   sort key.*

   > ⚠️ **Correction (probed §7c.4, 2026-07-22).** The original wording — "`render_priority` carries a *single*
   > value … submission order controls within … removes the ±127/256-run cap entirely" — is **wrong on the
   > mechanism**, in two grounded ways: (1) `sort_by_priority()` uses `SortArray` = **introsort = unstable**, so
   > equal `render_priority` does **not** preserve submission/caller order; (2) `render_priority` is clamped to
   > **[-128,127]** and the sort key is **8-bit**, so per-run distinct priorities cap at **256 runs**. The
   > achievable mechanism that still satisfies requirement 3: **distinct `render_priority` per run** (deterministic,
   > depth-independent — `prio_beats_depth`→0.30) for **inter-run** order, capped at 256 runs; **MultiMesh
   > instance-buffer order** for **intra-run** order, uncapped. Spike 2's real evidence was always the
   > *distinct*-priority `prio_*` tests, not equal-priority stability.
   >
   > **DECISION (Aaron, 2026-07-23): engine change for uncapped order.** ✅ **Built + verified.** The fold list
   > is now sorted by the instance's **`sorting_offset`** (a per-instance `float`, set via
   > `GeometryInstance3D.sorting_offset` / `instance_set_pivot_data`), *not* material `render_priority`. The game
   > stamps each run-instance's `sorting_offset = OTDepthPrimOrder run index` — **uncapped** (float, no ±127
   > clamp) and per-instance (not shared per-material). Implementation: new `SortByFoldOrder` comparator +
   > `sort_by_fold_order()` (`render_forward_clustered.h`) reading `owner->sorting_offset`, replacing the
   > 8-bit-`priority` sort for `RENDER_LIST_COMPOSITOR_FOLD`. `sorting_offset` normally perturbs depth-sorting,
   > but the fold list never depth-sorts and its prims are `depth_draw_never`, so repurposing it as the explicit
   > fold-order key is side-effect-free. Cost: **zero new public API** (reuses an existing per-instance channel).
   > Within a run, MultiMesh instance-buffer order still carries intra-run order. **Verified:** the
   > `uncapped_order` probe folds 300 runs (offsets 0..299) with a `sub` at order 299 folding *last* → 0.2845,
   > exactly the depends-on-order result; `render_priority` cannot even *express* order 299 (Material errors on
   > >127).
6. **"Touched" test (was §7.4) — coverage-α via engine pipeline override.** Coverage lives in the scratch's
   alpha channel exactly as shipped (seed α=0, discard `coverage==0` in Pass C → background stays pristine).
   The shipped mechanism needs the **alpha blend equation decoupled from the color blend** (SUB subtracts
   color but must **ADD** coverage, else a sub-heavy pixel drops to α≤0 and is falsely discarded; MIX reuses
   frag α for both the `SRC_ALPHA` color factor and coverage). Stock Godot material `blend_sub`/`blend_mix`
   set color+alpha together and **cannot** express this — but the engine's routed fold pipeline is new code
   (Spike 1 builds it), so it **forces alpha `ADD/ONE/ONE` independent of the material's color mode**. Result:
   **Pass C is unchanged.** *Fallback if the override proves infeasible (see 7c.2): a stencil touched-mask*
   *(fold pipeline writes `stencil=1`, Pass C rewritten to stencil-test `EQUAL 1`).*
7. **Display gamma — single curve.** display == sRGB == screen gamma, treated as one curve. The tiny
   sRGB-vs-BT.1886 mismatch cancels on the round trip (prims authored in, and viewed through, the same
   encode) and is far below the RGB555 quantization floor. Documented as an accepted approximation so it is
   not "corrected" into a mismatch.

### 7b. Resolved by implication (write down, don't re-decide)

- **Pass slot (was §7.2):** compositor seeds at **PRE_TRANSPARENT** → engine folds during the transparent
  pass → compositor resolves at **POST_TRANSPARENT**. Forced by 7a.1 + 7a.2. (Exact renderer confirmation
  is a 7c task.)
- **Seed encode (was §7.6):** the compositor performs the Pass A linear→display copy (it owns the buffer).
- **Occlusion (was §7.7):** already works — the fold prims are `depth_draw_never` + depth-tested against the
  opaque scene (`depth_draw_opaque` units), so they occlude correctly and never corrupt the depth buffer.
  Independent of the coverage-α channel.

### 7c. Parked — engine-facts to read & empirical probes (next phase, not chair-answerable)

1. ✅ **Raw-value preservation (GATING) — PASS (probed 2026-07-22).** Forward+ passes a raw display-texel
   fragment→scratch with **no color-space conversion**. Probe: `control_add` emits `ALBEDO=0.4` (raw display
   value) through a `compositor_fold`, `unshaded`, `blend_add` material into the black scratch; `fold_probe.gd`
   reads the `A2B10G10R10_UNORM` center pixel back as **R=0.3998**. That 0.0002 delta is *only* 10-bit UNORM
   quantization (0.4·1023 = 409.2 → 409/1023 = 0.39980). An sRGB **encode** would have read ~0.665; a **decode**
   ~0.133; we got identity. So raw display texels survive the Forward+ fragment→attachment path unchanged, and
   the color-space-agnostic-blender premise (§3) holds on real hardware (RTX 5090, Vulkan 1.4). *Gate cleared —
   proceed to code.* (Original wording: "does a raw display-texel fragment reach the scratch byte-for-byte?" —
   answer: yes, modulo the unavoidable 10-bit store, which is the same quantization the shipped Pass C applies.)
2. ✅ **Alpha-blend independence — PASS (probed + implemented 2026-07-22).** The routed fold pipeline *can*
   force alpha `ADD/ONE/ONE` independent of the material's color blend mode — **decision 7a.6 confirmed, no
   stencil fallback.** Grounding: `blend_mode_to_blend_attachment` (material_storage.cpp) shows stock
   `blend_sub` uses `alpha_blend_op = REVERSE_SUBTRACT`, so a sub prim *subtracts* coverage. Observed the bug
   on hardware: `order_b` (add 0.6 then sub 0.3) left a **touched** pixel R=0.3 with **A=0.0** — shipped Pass C
   would falsely discard it. Fix: in `scene_shader_forward_clustered.cpp::_create_pipeline`, when the
   ShaderData's `compositor_fold` flag is set, override the attachment's alpha fields to
   `alpha_blend_op=ADD, src_alpha=ONE, dst_alpha=ONE` while leaving the color blend (add/sub/mix) untouched —
   Vulkan blends color and alpha with independent ops/factors, so this is a legal single-attachment state.
   After rebuild, `order_b` reads R=0.3, **A=1.0** (coverage accumulates; color still subtracts); `control_add`
   / `order_a` colors unchanged. The `compositor_fold` flag already distinguishes the fold pipeline variant, so
   the override is keyed for free and Pass C stays exactly as shipped. *(This is also the first slice of the
   engine Pass B implementation.)*
3. ✅ **`blend_mix` semantics — PASS (probed 2026-07-22).** Godot `blend_mix` + a material emitting α=0.5
   reproduces the compositor's `0.5·src + 0.5·dst`. Grounding: the `BLEND_MODE_MIX` attachment is
   `color = src·SRC_ALPHA + dst·(1−SRC_ALPHA)`, which at α=0.5 is the half-mix. Hardware probes:
   `mix_over_black` (mix 0.6 @α=0.5 over black) → **R=0.3001** = `0.5·0.6 + 0.5·0`; `mix_half`
   (add 0.8 seed, then mix 0.2 @α=0.5) → **R=0.5005** = `0.5·0.2 + 0.5·0.8`. Both exact to the 10-bit floor.
   The §7c.2 coverage-α override left the mix *color* untouched (it rewrites only the alpha channel), so mix
   still both blends color correctly and registers coverage (A nonzero in both cases).
4. ⚠️ **Routed-draw ordering — INVESTIGATED (2026-07-22); partially CONTRADICTS 7a.5 as worded — see the
   correction note under 7a.5.** What holds: the fold list is drawn **after the transparent resolve, right
   before the POST_TRANSPARENT callback** (`render_forward_clustered.cpp::_render_scene`, immediately above
   `_process_compositor_effects(…POST_TRANSPARENT…)`), depth-tested against resolved scene depth, and it is
   **never sorted by camera depth** (`sort_by_priority()` has no depth term). What FAILS: (a) `sort_by_priority`
   calls `SortArray::sort`, which is **introsort — unstable**, so *equal* `render_priority` does **not**
   preserve caller/submission order (`order_a`→0.60, `order_b`→0.30 are introsort-arbitrary, not caller order);
   (b) material `render_priority` is clamped to **[-128, 127]** (`MATERIAL_RENDER_PRIORITY_MIN/MAX`) and the
   sort key packs `priority : 8` bits, so **distinct-priority-per-run caps at 256 runs** — the ±127 cap is
   *not* removed. Grounded resolution: order must be carried by **distinct `render_priority` per run**
   (deterministic, depth-independent — `prio_beats_depth`→0.30 proves it) capped at 256 runs, **plus MultiMesh
   instance-buffer order within a run** (uncapped, hardware-ordered). See the 7a.5 correction + open decision.

---

## 8. Relationship to the existing spikes (what carries over)

- **Spike 1** (`compositor_fold` render_mode routing flagged transparents into a separate render list drawn
  into a scratch): **reused** — the routing + separate-list machinery. *Changes:* the scratch is
  **display-seeded and shared**, not black/isolated; and **no per-step linear clamp** is needed (the
  materials already emit display values, so UNORM saturation on display values *is* the PSX clamp).
- **Spike 2** (`sort_by_priority()`): **superseded by §10's `sort_by_fold_order()`.** Spike 2's real, holding
  finding was that *distinct* priorities give deterministic, depth-independent order (the `prio_*` tests). Its
  "equal priority → stable caller order" claim was **false** (introsort is unstable — §7c.4). The fold order is
  now carried by the per-instance **`sorting_offset`** float (uncapped), not the 8-bit `render_priority`; the
  no-depth-sort principle carries over directly.
- **Spike 3** (MultiMesh instance-buffer transport + per-instance data): **reused** as the prim transport
  (the compositor's real interface is a raw instance-buffer RID; MultiMesh is the caller's vehicle).

---

## 9. References (shipped code, read 2026-07-22)

- **FFT** `fft-monorepo-compositor/godot-learning`: `src/effects/CombatDisplaySpaceComposite.gd`,
  `src/effects/OTDepthPrimOrder.gd`, `src/effects/UnifiedPrimStager.gd`,
  `assets/shaders/combat_displayspace_composite.glsl`, `assets/shaders/unit.gdshader`,
  `assets/shaders/unit_sprite_body.gdshaderinc`.
- **Engine:** `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl:2354-2361`
  (mobile outputs linear ÷ luminance_multiplier — no free display-space blend); the Forward+ Spike 1–3 diff
  on this branch.

---

## 10. Pass B — built + verified (2026-07-23)

The engine change is complete on branch `spike/compositor-consume-material-output` (working tree, **uncommitted**).
It is a **thin** patch: the engine only draws the flagged prims into a compositor-owned scratch. Passes A and C
stay in userland. Four pieces, each verified on hardware via `/tmp/spike-fold/proj` (RTX 5090 / Vulkan 1.4 / Forward+).

**Engine diff (all in `renderer_rd/forward_clustered/`, plus the `compositor_fold` render_mode registration in `servers/rendering/shader_types.cpp`):**

1. **Coverage-α override** (`scene_shader_forward_clustered.cpp::_create_pipeline`). When a ShaderData's
   `compositor_fold` flag is set, the color-blend attachment's **alpha** fields are forced to
   `alpha_blend_op=ADD, src_alpha=ONE, dst_alpha=ONE`, leaving the material's *color* blend (add/sub/mix)
   untouched. Decouples the "touched" coverage mask from the color blend so `blend_sub` doesn't drive coverage
   to 0 (§7a.6). *Verify:* `order_b` A: 0.0 → **1.0**, color unchanged.
2. **Stateless RID handoff** (`render_forward_clustered.cpp::_render_scene`, fold-pass block). The engine no
   longer creates/clears its own scratch. It looks up the **compositor-owned** `compositor_fold`/`color` named
   texture (allocated + seeded by Pass A at PRE_TRANSPARENT), builds the framebuffer from it + scene depth, and
   draws with **`DRAW_DEFAULT_ALL` (LOAD — preserves the seed)**. If the texture is absent, it warns and skips
   (the compositor must own it). *Verify:* `seed_only` → **0.20** (seed survives untouched); `seed_add` →
   **0.50** = seed 0.2 + fold 0.3 (LOAD, not clear — a clear would read 0.30).
3. **Uncapped order** (`render_forward_clustered.h` `SortByFoldOrder`/`sort_by_fold_order()` + call site). Fold
   list sorts by `owner->sorting_offset` (per-instance float, uncapped), replacing the 8-bit `render_priority`
   sort. See §7a.5 decision note. *Verify:* `uncapped_order` (300 runs, sub at offset 299 folds last) → **0.2845**.
4. **Guard-rail asserts** (fold-pass block). `WARN_PRINT_ONCE` if the viewport tonemapper ≠
   `ENV_TONE_MAPPER_LINEAR` (§7a.3), if `internal_size ≠ target_size` (§7a.4 upscaling), or if MSAA is enabled
   (§7a.4). Non-fatal — the fold still runs. *Verify:* `guardrail_agx` (forces AgX) prints the tonemapper
   warning and still folds (0.3998). The size/MSAA guards share the same pattern (positive-triggering them needs
   a scaled/MSAA `SubViewport`, not scaffolded).

**Spike harness now models the real architecture** (`/tmp/spike-fold/proj`): a `fold_seed.gd` CompositorEffect
plays **Pass A** — at PRE_TRANSPARENT it `create_texture`s the `compositor_fold`/`color` scratch (idempotent by
name; usage includes `CAN_COPY_TO` for `texture_clear`) and seeds it each frame; `fold_probe.gd` plays the
**Pass C** reader at POST_TRANSPARENT. Prim helpers set `sorting_offset` (the fold order), not `render_priority`.
A black seed reproduces the old clear-to-black exactly, so all pre-existing probes are unchanged.

**What's left for the real integration (not in this spike):** Pass A's seed is a real *scene-color* copy
(linear→display, coverage α=0) rather than a flat `texture_clear`; Pass C is the shipped dither/palette/RGB555
+ coverage-discard resolve; and the game stamps `sorting_offset = OTDepthPrimOrder run index` on real
MultiMesh run-instances. The engine contract they depend on is done and proven.
