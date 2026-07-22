# Design: engine-folded compositor prims (`compositor_fold`)

**Status:** 🟢 Design converged (this document) · MVP mechanism verified on-GPU (see feasibility §8)
**Branch:** `spike/compositor-consume-material-output`
**Engine tree:** Godot **4.8-dev**
**Owner:** Aaron Curry
**Last updated:** 2026-07-22

> Companion docs:
> - `compositor-consume-material-output-feasibility.md` — the "can/should we" analysis + the Spike 1 log and on-GPU verification. **Read that for *why*; read this for *what we're building*.**
> - Sibling blend-space effort: `../godot` @ `spike/forward-plus-display-space-additive`, `docs/display-space-additive-blending.md`.
> - Real downstream consumer (the thing this serves): the FFT combat compositor at `/home/curry/Repos/fft-monorepo-compositor` (`godot-learning/`). See §7.

---

## 1. The goal (one paragraph)

A project shades transparent "prims" with a Godot `ShaderMaterial` (`.gdshader`) and needs a
`CompositorEffect` to consume the **self-shaded, depth-ordered, per-step-clamped** result — an
interleaved add/sub fold over hundreds-to-thousands of flat, single-depth prims — **without
re-implementing each material's fragment shader in raw GLSL**, and **without** maintaining a second
copy of the depth math. Today the FFT project does exactly this by hand: a raw-RD compositor shader
re-ports the prim shading *and* the occlusion-depth formula. This design removes both duplications by
letting the **engine draw the flagged materials through their real pipeline** into a clamping scratch
the compositor reads.

---

## 2. The key realizations that make it cheap

These are the load-bearing findings from the design conversation. Each one removes something that
looked required but isn't.

1. **No A-buffer.** The prims are flat and each sits at a single depth → they never interpenetrate →
   **a single global fold order exists** (the same order at every pixel). Per-pixel fragment
   separation (an A-buffer, the walled R2 feature) is only needed when the order differs *between*
   pixels. It doesn't here. So "reorder every prim" = "fold all prims in one chosen sequence," which
   the rasterizer already does by draw order. Scales to thousands — it's just a draw list.

2. **Depth and age never cross to the GPU.** They are **CPU sort inputs**, consumed *into* the fold
   order before any draw. Once the prims are sorted, the fold shader draws them in sequence and needs
   neither. The sorted **sequence itself is** the depth+age information. (So "pass all compositor data
   through one buffer" is satisfied without depth/age riding along — the order carries them.)

3. **One linear Ordering-Table depth.** There is only one depth *model* — representative point →
   camera distance, shifted by the depth mode. It appeared "twice" only because two consumers read it
   on two rulers: the CPU bucket-sorter needs **linear view-space Z** (uniform 0.19-unit buckets;
   NDC collapsed every prim into one bucket — the #212 bug), while the GPU occlusion test speaks the
   depth buffer's **NDC/reversed-Z**. The NDC encoding is *imposed by the hardware depth buffer*, not
   chosen. Reversing the scene depth back to linear is **lossless for the ortho combat camera**, which
   lets occlusion run in linear space too — collapsing the model to one number end-to-end.

4. **The sort is one composite key in one place.** Depth-bucket and age are not two sorts in two
   locations — they are one LSD-radix pass (age secondary, depth-bucket primary+stable, submission
   tertiary) producing **one linear sequence**. That sequence's index *is* the per-prim order key the
   engine folds by.

---

## 3. The architecture

```
┌──────────────────────────────────────────────────────────────────┐
│ PROJECT (GDScript) — ALL PSX policy stays here                     │
│                                                                    │
│  producers ─► stage prim {transform, color, uv, blend_mode}        │
│     │                                                              │
│     │  ot_order_z = (view · centroid).z + mode_bias  ◄─ ONE linear │
│     │  age        = frames since spawn                  OT depth   │
│     ▼                                                              │
│  OTDepthPrimOrder.order()  ─ radix: depth bucket → age → submit    │
│     │                                                              │
│     ▼                                                              │
│  ordered list  +  per-prim ORDER KEY ───────────────────┐         │
└──────────────────────────────────────────────────────────┼────────┘
        depth & age are CONSUMED into the order here;       │ order key
        they never cross to the GPU (the sequence IS them)  │ + material
                                                            ▼
┌──────────────────────────────────────────────────────────────────┐
│ ENGINE (Forward+) — new capability                                 │
│                                                                    │
│  RENDER_LIST_COMPOSITOR_FOLD ─ sorted by YOUR order key            │
│     │   (materials flagged `render_mode compositor_fold`)          │
│     ▼                                                              │
│  draw each prim THROUGH ITS REAL .gdshader ──────────────┐        │
└──────────────────────────────────────────────────────────┼────────┘
                                                            ▼
┌──────────────────────────────────────────────────────────────────┐
│ GPU — the .gdshader  (SINGLE source of shading + depth)            │
│                                                                    │
│  • shade fragment  ── your real prim logic, no GLSL re-port        │
│  • occlude ── sample scene depth ─► REVERSE to linear view-Z       │
│               (lossless, ortho) ─► compare vs prim Z ─► discard    │
│  • output color + per-material HW blend (add / sub)                │
│     │                                                              │
│     ▼                                                              │
│  A2B10G10R10_UNORM scratch ── per-step DISPLAY-SPACE clamp         │
│     drawn in your order = THE FOLD (saturation = PSX clamp)        │
└──────────────────────────────────────────────────────────┬────────┘
                                             folded, clamped, ordered
                                                            ▼
┌──────────────────────────────────────────────────────────────────┐
│ COMPOSITOR (CompositorEffect, POST_TRANSPARENT)                    │
│                                                                    │
│  read ("compositor_fold","color") ─► dither / palette / composite │
│                                                                    │
│  ✗ no fold GLSL   ✗ no shading re-port   ✗ no NDC depth math       │
└──────────────────────────────────────────────────────────────────┘
```

### What each stripe removes
- **Shading duplication** → gone: the prim shades through its own `.gdshader`; the hand-ported fold
  GLSL (`combat_displayspace_composite.glsl`'s shading) disappears.
- **NDC depth triplication** → gone: one linear `ot_order_z` computed forward for ordering (CPU); for
  occlusion the shader **reverses** the scene depth to linear (lossless on the ortho cam). No
  `ot_depth`, no `.gdshaderinc` hand-port in raw GLSL.

---

## 4. Where the sort lives (the ordering contract)

- **All ordering is CPU, before the draw**, in the project's `OTDepthPrimOrder` (or equivalent).
  Depth bucket **and** the within-bucket age tie-break are one composite radix key producing one
  sequence. The compositor **never sorts** in this design.
- That sequence surfaces to the engine as an **already-ordered prim array**; the engine draws it in
  array order and **does not sort at all** (the spike's `sort_by_reverse_depth_and_priority` is deleted,
  not parameterized). Age is baked into the order — the engine is order-agnostic.
- **Why not in the compositor:** the compositor sees the scratch *after* the fold; the order is baked
  in at draw time and can't be undone (that's the A-buffer wall again). The sort must *precede* the
  draw → CPU, never compositor-after-fold.

---

## 5. The engine ↔ project boundary (do not overfit)

| Engine (general capability) | Project (policy — must NOT leak into the engine) |
|---|---|
| `compositor_fold` render_mode + fold-into-clamping-scratch | `OTDepthPrimOrder`, the radix sort |
| Fold draws prims in **caller-submitted array order** (engine never sorts) | Bucket width `0.19`, OT span `383`, mode→bias table |
| Per-step **display-space (sRGB) clamp** (spec constant) | Age tie-break, the #212/DEMI ordering contract |
| Occlusion: reverse-scene-depth *or* hardware depth test | dither / palette / final composite |
| Fold **instanced** surfaces (per-instance data) in submitted order | how prims are staged into runs |

The engine only ever learns: *"fold these flagged materials, in this order, clamped, into a buffer you
can read."* Everything PSX-specific stays in the project.

---

## 6. Engine work list

1. **`compositor_fold` render_mode + fold-into-clamping-scratch** — ✅ **done & verified on-GPU**
   (feasibility §8: occlusion, per-step UNORM clamp, depth-ordered add↔sub, temporal stability,
   selective routing).
2. **Ordered-array submission** (replaces "caller order key"). The engine does **not** sort — the
   game hands it an **already-ordered** prim array (produced by `OTDepthPrimOrder`) and the engine
   draws it in that order. So the spike's `sort_by_reverse_depth_and_priority` is **deleted**, not
   parameterized — *fewer* touch points, and it matches how the game already stages. Because a
   MultiMesh's instances are drawn in buffer order, an ordered flagged MultiMesh preserves the game's
   order for free. **Cross-run** order (add↔sub boundaries, which can't share one MultiMesh since HW
   blend-op is pipeline state) is carried by the material's existing **`render_priority`** = the run's
   `OTDepthPrimOrder` index; the fold list sorts by priority only (no depth). ← the real unlock.
   ✅ **Prototyped & verified on-GPU (Forward+), 2026-07-22** (feasibility §8, Spike 2). Bound:
   `render_priority` is ±127 → up to ~256 ordered runs, ample for the FFT's handful.
3. **Display-space sRGB clamp** spec-constant so the per-step clamp is gamma-space (copy the sibling's
   `linear_to_srgb` + `sc_*` pattern). *Not built.*
4. **Occlusion mode** — either bind scene depth as a **sampler** (manual-linear reverse; the pure
   one-OT-depth model) or keep the **depth-attachment** hardware test (spike already does this). *Decision below.*
5. **Instanced prim path (in submission order)** — thousands of prims arrive as instanced *runs*, not
   scene-tree nodes. "MultiMesh" is only the *buffer vehicle* (the instance-buffer layout as a transport
   for per-prim data), not a logical concept the engine needs. The engine needs only "instanced draw of
   prims through a material, with per-instance data, **in array order**." The current spike routes
   per-surface scene instances; this needs genuine design. *Not built.*

Follow-ups already noted in feasibility §8: `blend_sub` scratch-seeding (subtractive from a black base
clamps to 0).

**Mobile renderer — priced, viable (2026-07-22).** A read-only investigation of `forward_mobile/`
confirmed the fold ports as a **near-clean mirror** of Forward+, ~6–8 touch points. The fold draws at
**POST_TRANSPARENT**, which mobile supports (only POST_OPAQUE/POST_SKY are unsupported). A
POST_TRANSPARENT effect leaves `merge_transparent_pass = true` (only PRE_TRANSPARENT breaks the merge)
and merely disables subpass post-process, so the subpass merge is **not** sacrificed by the fold; the
subpass container closes naturally at `draw_list_end()` (`render_forward_mobile.cpp:1311`) and both
merge branches converge to **one common insertion point** before the POST_TRANSPARENT dispatch
(`:1381`) — structurally the same seam Forward+ uses. Two bounded caveats, not vetoes: (a) tiled-GPU
bandwidth of the extra depth-load + scratch-store pass (a documented perf cost, same shape as Forward+'s
separate pass); (b) on MSAA without hardware depth-resolve, occlusion depth is unavailable (existing
engine limitation, WARN'd at `:1315`) → guard the fold there. **Mobile does not veto "few touch points."**

---

## 7. The one open decision — occlusion

| | Manual-linear (recommended for fidelity) | Hardware test (less engine work) |
|---|---|---|
| Occlusion | shader samples scene depth, **reverses to linear**, compares vs prim linear Z, `discard` | shader writes biased `DEPTH` via shared `.gdshaderinc`; hardware z-test |
| Depth model | **pure single linear OT** — no NDC anywhere in project logic; matches the real PSX (no z-buffer, one Ordering Table) | keeps NDC, but confined to one shared include |
| Engine plumbing | bind scene depth as a **sampler** | bind as **depth attachment** (already implemented in the spike) |
| Cost note | already writing custom `DEPTH` ⇒ no early-Z loss either way; lossless on the ortho cam | simplest path from today's spike |

Both kill the raw-GLSL hand-port. Manual-linear delivers the one-OT-depth model; hardware-test is the
smaller step from the working spike. **Leaning manual-linear** for PSX faithfulness — pending final call.

---

## 8. Why End-state A (engine folds), not B (compositor/GPU sorts)

- **A (this design):** engine folds through the real material in the project's CPU-computed order.
  Kills both duplications, no A-buffer, no GPU sort, scales to thousands, byte-identical to the current
  hand-rolled fold (single global order). Gives up only the ability to re-fold the *same* prims in
  *several different* orders in one frame — which the pipeline doesn't do.
- **B (rejected for now):** upload prims *unsorted* carrying depth+age, sort on the **GPU**, fold in the
  compositor. The only world where depth/age must ride the buffer and the compositor owns the sort — but
  it reopens the per-prim capture/storage problem (toward the walled A-buffer). Revisit only if a future
  need genuinely requires post-submit reordering.

---

## Change log
- **2026-07-22** — Priced the **mobile** port (read-only investigation of `forward_mobile/`): near-clean
  mirror of Forward+ (~6–8 touch points, one common insertion point at the POST_TRANSPARENT seam), not a
  veto; two bounded caveats (tiled-GPU bandwidth, MSAA depth-resolve). Folded in the submission-model
  refinements: engine **does not sort** (deletes `sort_by_reverse_depth_and_priority`) — the game submits
  an **already-ordered array**; MultiMesh is only the buffer vehicle, not a concept. Reworded work items
  #2 (order key → ordered-array submission) and #5, §4 (sort ownership), and the §5 boundary table.
- **2026-07-22** — Initial design writeup. Converged on End-state A after mapping the FFT consumer's
  CPU depth/age sort (`OTDepthPrimOrder`) and its CPU↔GPU depth duplication: single global order ⇒ no
  A-buffer; depth+age are CPU sort inputs consumed into the order; one linear OT depth with lossless
  reverse for occlusion on the ortho cam. Enumerated the engine work list and the engine/project
  boundary. Open items: caller order key (#2), multimesh path (#5), occlusion mode (§7).
