# Plan: engine-shaded, display-space, ordered fold (into a compositor-shared scratch)

**Status:** 🟡 Design in progress — converged through grounded reading of the *shipped* FFT compositor.
Basis for further discussion, not yet built. **Supersedes** the "engine owns the fold, from black, in
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

**Pass ownership (proposed):**
- **Pass A (seed)** — copy the free opaque scene into the scratch as display. *Compositor or engine (open, §7).*
- **Pass B (fold)** — **the new engine capability:** draw real-material prims into the display scratch,
  existing `add`/`sub`/`mix`, in order, depth-tested. *Engine.*
- **Pass C (out)** — dither / palette / RGB555 quantize / coverage discard. *Compositor.*

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

## 7. Open questions to resolve (before/while building)

1. **Raw-value preservation — PROTOTYPE FIRST.** Does the engine's fragment→scratch write preserve raw
   display values on Forward+ (no linear↔sRGB conversion, no scaling)? Cheap to probe with the existing
   `fold_probe.gd` harness: draw a raw-texel material outputting a known display value, read back the
   scratch, confirm the number survives byte-for-byte.
2. **Buffer sharing + pass ordering.** Seed (A) must precede the engine fold (B) which must precede
   copy-out (C). Compositor effects fire at fixed callbacks (`PRE_/POST_TRANSPARENT`). Where does Pass B
   slot, and who owns/creates the shared scratch? Candidates: (i) engine does A+B, compositor does C at
   POST_TRANSPARENT; (ii) compositor seeds at PRE_TRANSPARENT, engine does B, compositor does C.
3. **`blend_mix` semantics.** The compositor's MIX = `0.5*src + 0.5*dst` via `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`
   with the fragment emitting α=0.5. Does Godot's `blend_mix` + a material emitting α=0.5 reproduce it?
4. **Coverage α channel.** The compositor accumulates coverage in α (separate `ADD/ONE/ONE` alpha blend)
   so Pass C can discard untouched pixels. Can the engine's draw maintain that coverage channel, or does
   Pass C need a different "untouched" test?
5. **`render_priority` range.** ±127 (256 values). Enough for the number of runs (DEMI ≈ tens)? If a scene
   ever needs more ordered runs, a wider order key is required.
6. **Seed encode + "free" opaque scene.** The opaque scene is linear in Forward+; seeding needs
   linear→display. Confirm that copy is clean and who does it.
7. **Occlusion.** Already works (depth-test vs opaque scene, Spike 1). Confirm it composes with the shared
   scratch and the ordered draw.
8. **Scratch ownership.** CompositorEffect-owned named buffer vs. engine named buffer on `RenderSceneBuffersRD`.

---

## 8. Relationship to the existing spikes (what carries over)

- **Spike 1** (`compositor_fold` render_mode routing flagged transparents into a separate render list drawn
  into a scratch): **reused** — the routing + separate-list machinery. *Changes:* the scratch is
  **display-seeded and shared**, not black/isolated; and **no per-step linear clamp** is needed (the
  materials already emit display values, so UNORM saturation on display values *is* the PSX clamp).
- **Spike 2** (`sort_by_priority()` = caller order): **reused directly** — `render_priority` = the
  `OTDepthPrimOrder` run index delivers requirement 3.
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
