# Compositor Generalization — RD/Vulkan Feasibility (deep-research, 2026-07-19)

Feasibility study for generalizing the proven single-atom formation display-space
compositor into a PSX-faithful **N-primitive ordered blend orchestrator** as a Godot 4.6
`CompositorEffect`. Companion to the handoff
`/tmp/handoff-compositor-generalize-psx-blend.md` and the color-space living doc
`FORMATION_ORB_ADDITIVE_COLORSPACE.md`.

Method: `deep-research` harness — 6 search angles, 26 sources fetched, 111 claims
extracted, 25 adversarially verified (3-vote, 2/3 to kill). **Caveat:** a mid-run
Anthropic API rate-limit storm forced ~45 verifier votes to abstain, so a batch of
otherwise-standard Vulkan-spec facts landed **0-0 (unratified)** rather than confirmed.
Those are flagged below and must NOT be cited as verified by this run.

---

## Verdict

**Building the N-fold compositor is feasible on the Mobile renderer as a raster (not
compute) ping-pong.** Every *engine-API* enabler the atom relies on is confirmed by
primary sources. The remaining risk is **algorithmic** (sort key + commutative-run
equivalence), which is a `prototype` question, not a further-research question.

**Cost is now settled too (perf prototype, 2026-07-19 — see "Perf prototype RESULTS"
below).** Per-pass cost is linear in N with no cliff; the full-screen vs scissor curves
DIVERGE (fill/bandwidth-bound, not fixed-overhead-bound), so **region-local scissoring is
the default architecture and the cap/commutative-run-grouping machinery is NOT needed** —
the unified fold stands alone. Cap/grouping shelved (top-N remains the policy only if ever
resurrected). MultiMesh/ADR-0040 integration is now the sole remaining design risk (its
own session).

---

## Confirmed (primary sources, surviving adversarial verification)

### Q1 — Ping-pong RMW works; preserve is automatic
A K-pass `scratch_k → scratch_{k+1}` ping-pong is viable, and **preserve-between-passes
is inferred from write-then-sample usage** — you no longer hand-specify
`RDInitialAction`/`RDFinalAction`. Godot 4.4+ `draw_list_begin()` auto-resolves initial
and final action via the render graph after command reordering.
- Source: [godot#98670](https://github.com/godotengine/godot/pull/98670) (3-0)
- Caveat from the PR: mark textures `is_transient` to recover TBDR bandwidth where
  applicable; old `draw_list_begin` signatures remain via compat wrappers.

### Q1 — CompositorEffect is supported on Mobile
Officially supported on **Mobile and Forward+** (not Compatibility). The Mobile
dependence is a documented-supported configuration, not a hack.
- Source: [4.6 compositor docs](https://docs.godotengine.org/en/4.6/tutorials/rendering/compositor.html) (3-0)

### Q1 / Q5 / Q6 — Compute ping-pong on Mobile is impossible → raster is the only path
Mobile's internally-created render buffers do **not** carry `TEXTURE_USAGE_STORAGE_BIT`
(project-grounded: color layer usage = 515 = SAMPLING | COLOR_ATTACHMENT |
INPUT_ATTACHMENT). Binding a render buffer into a compute set fails with
`Image (binding: 0, index 0) needs the TEXTURE_USAGE_STORAGE_BIT usage flag`.
Works on Forward+, fails on Mobile.
- Sources: [godot#96737](https://github.com/godotengine/godot/issues/96737),
  [#108781](https://github.com/godotengine/godot/issues/108781),
  [#99493](https://github.com/godotengine/godot/issues/99493) (3-0)

### Q6 — A2B10G10R10_UNORM cannot be a storage image on Mobile
Using it as a storage image requires the **optional** Vulkan
`shaderStorageImageExtendedFormats` capability (same for R8). Godot only requests STORAGE
on the clustered/Forward+ renderer, never Mobile. Reinforces: the format is a **color
attachment** (hardware blend + free UNORM saturation clamp), never compute-writable.
- Source: [godot#71939](https://github.com/godotengine/godot/pull/71939) (2-0)

### Q4 — Stencil-based occlusion is fully supported (and the depth-write coupling is real)
`RDPipelineDepthStencilState` exposes independent `enable_depth_test` /
`enable_depth_write` booleans (both default false) **plus** full front/back stencil
configuration — `front_op_compare`/`back_op_compare` (CompareOperator, default 7=ALWAYS),
per-face `op_fail`/`op_pass`/`op_depth_fail` StencilOperations, and
reference/compare/write masks. `COMPARE_OP_NOT_EQUAL` maps 1:1 to `VK_COMPARE_OP_NOT_EQUAL`.
- **Confirms the gotcha:** `enable_depth_write` "only works when `enable_depth_test` is
  also true" → depth-write is gated on depth-test, which is exactly why the atom uses
  stencil (not `depth_test_disabled`) to avoid reordering formation units into the opaque
  pass.
- Sources: [RDPipelineDepthStencilState (4.6)](https://docs.godotengine.org/en/4.6/classes/class_rdpipelinedepthstencilstate.html),
  [(stable)](https://docs.godotengine.org/en/stable/classes/class_rdpipelinedepthstencilstate.html) (3-0)

### Q4 — POST_TRANSPARENT is the right (only documented, pre-tonemap-adjacent) hook
CompositorEffect callbacks hook only into 3D-pipeline stages. The five enums are
PRE_OPAQUE(0), POST_OPAQUE(1), POST_SKY(2), PRE_TRANSPARENT(3), POST_TRANSPARENT(4).
POST_TRANSPARENT fires after the transparent pass, before built-in post-processing/tonemap.
**There is no post-tonemap hook in 4.6** — a strictly post-tonemap display-space fold is
not achievable; POST_TRANSPARENT (pre-tonemap-adjacent) is the correct point, consistent
with the LINEAR-tonemap atom.
- Sources: [4.6 docs](https://docs.godotengine.org/en/4.6/tutorials/rendering/compositor.html),
  [proposal#13115](https://github.com/godotengine/godot-proposals/issues/13115) (2-1)

### Useful refutation for Q4 depth-scene integration
The claim that the **depth texture is inaccessible even on Forward+** was **refuted
(0-2)**. Depth-access difficulty is a Mobile-STORAGE-bit problem, not a Forward+ blocker —
relevant if the depth-scene generalization moves off Mobile for the combat case.
- Source: [godot#99493](https://github.com/godotengine/godot/issues/99493)

---

## Prototype verdict (2026-07-19) — Q3 SETTLED, Q2 sort settled (tie-break pending oracle)

Throwaway numeric model `godot-learning/tools/proto_ordered_fold.gd` (5-bit integer PSX
mode math — the faithful ground truth, since the PSX re-reads the RGB555 framebuffer per
primitive). Run headful: `godot --path godot-learning --script res://tools/proto_ordered_fold.gd`.

- **Q3 grouping equivalence — PROVEN byte-exact.** 200,000 random trials (mixed modes,
  1–6 prims, random B): **0 mismatches** between the naive per-primitive clamped left-fold
  and the grouped fold. Collapsing maximal **same-direction a==1 runs** into one
  accumulate+clamp is integer-exact:
  - **adds = {mode 1 (B+F), mode 3 (B+¼F)}** → `B = min(B + Σf_eff, 31)` (mode-3 pre-shifts
    its own F by 2). Single-sided (ceiling-only) saturation is associative → groupable.
  - **subs = {mode 2 (B−F)}** → `B = max(B − Σf, 0)`. Floor-only saturation, mirror case.
  - No intermediate-quantization hazard: integer add/sub within a run has no fractional
    rounding, so grouping needs no per-prim re-quantize to stay exact.
- **Q3 where clamp breaks — CHARACTERIZED.** Two hard sequential boundaries a run may not
  cross: (1) **every add↔sub transition** — e.g. B=25, F=10: add→sub=21 vs sub→add=25
  (clamp makes order matter); (2) **mode 0 (½B+½F)** — it rescales B (a=½), non-commutative
  even against itself (B=10, F=8 then 24 = 16 vs 24 then 8 = 12), so it is **always its own
  step**. Pass-count bound = number of add/sub/solo *segments*, i.e. real polyphony, not
  primitive count. ✓ confirms the handoff's Q4 grouping premise.
- **Q2 sort — submit order is irrelevant once sorted.** Fold result invariant under 5,000
  random submit orders when sorted by **(OT depth z DESC, submission-index DESC)**. The sort
  fully determines fold order.
- **Q2 tie-break — the ONE thing still to confirm against the live oracle.** The equal-z
  secondary key (reverse-submission = PSX AddPrim HEAD-insertion, per psx-spx/ordtbl.pdf)
  is asserted, not oracle-verified. Confirm via `effect-parity` against pcsx before build —
  it only matters when two composited prims share an exact z bucket.

Net: the compositor generalization is now de-risked on both the API axis (research) and the
algebraic axis (prototype). Remaining unknowns are a live-oracle tie-break check (Q2) and the
"does combat visibly need it" measurement (Q6) — neither blocks starting the build design.

---

## Design synthesis (grilling, 2026-07-19) — the architecture that fell out

Stress-tested the design with the user. Conclusions (anchor the ADR here):

- **Necessity settled.** The Mobile UNORM clamp only *saturates* [0,1]; it does NOT make the
  add faithful. Godot's hardware add is **linear-space**; PSX adds in **display (gamma)
  space** — divergence is midtone-large (display 0.5+0.5 → 1.0 white; linear → ~0.69). So
  display-space blending is required. DEMI (mixed add/sub meshes converging, randomized
  order) proves the *orchestrator* (not just per-effect display-space) is required.
- **DEMI's DEFINITE defect is color-space, not order** (corrected 2026-07-19 — the earlier
  "fails twice, both structural" was an overstatement the user caught). The certain, visible
  problem is (a) Godot's hardware add is **linear-space**, PSX is **display-space**. Ordering
  is a *separate, mostly-solved* concern:
  - **Occlusion** (which fragment is hidden) = the depth buffer, via `psx_ot_depth` (ADR-0009).
    GPU-side, already correct.
  - **Blend *sequence*** (how the visible transparent layers combine) = **draw-submission
    order**, which the engine ALREADY establishes (Godot native transparent sort +
    instance-buffer order). **ADR-0015 deliberately RETIRED the old explicit CPU sort as
    redundant** — and for the common case (plain ADD, commutative) order doesn't even matter.
  - The depth buffer does NOT sequence transparent blends (transparent frags don't depth-write
    against each other) — but it doesn't need to, because the draw order already does.
  - **Residual ordering risk (minor, empirical):** cross-mode-bucket interleave of a *single*
    effect's non-commutative prims — MultiMeshes batch per mode, so add-vs-sub order across the
    5 buckets is whatever the queue picks. DEMI empirically looks right, so this is minor;
    confirm via `effect-parity` only if a specific effect looks wrong. Ties into the parked
    MultiMesh/ADR-0040 session.
- **ONE render model, not several.** Opaque geometry → normal scene pass (it is the *canvas*
  + depth buffer, NOT a rival pipeline). ALL transparent blended prims → **one display-space,
  OT-ordered fold** in the compositor, depth-tested against the opaque buffer for occlusion.
  Grouping / batching-within-commutative-runs / any cap are **internal optimizations of that
  single fold**, added only if the perf prototype shows they're needed. "Just always use the
  compositor for the transparent stuff" is the correct instinct.
- **STP split is OFF the risk list.** ADR-0040 (`EffectMultiMeshPool`) already decomposes each
  effect into 1 opaque + mode0..3 MultiMeshes with a per-instance `semi_trans_on` flag. The
  opaque/semi split is a solved abstraction; MultiMesh is a *batching* mechanism, not an STP
  device. No per-primitive doubling.
- **Region-local fold = default architecture.** Union the overlapping transparent prims' CPU
  AABBs → the high-polyphony blob; ping-pong two *region-sized* scratch buffers seeded once;
  fold O(N) in the blob; composite back in one pass. Full screen touched exactly twice
  regardless of N. Fill cost → O(N × region_area), negligible for DEMI's small blob.
- **The fold INHERITS the engine's existing order — do NOT reintroduce a CPU sort.** The order
  already exists (`psx_ot_depth` occlusion per ADR-0009 + Godot native transparent sort +
  instance-buffer order per ADR-0015). **ADR-0015 explicitly deleted the old explicit CPU
  sort; reintroducing one would walk directly backwards against a deliberate decision.** The
  fold consumes prims **in that already-established order** (an O(n) walk of a key we already
  compute, NOT a new per-frame reorder). What the fold *changes* is **where the blend math
  happens — display-space RMW instead of hardware linear-space blend — NOT the order.** Its
  only genuinely new cost is the sequential RMW (un-batching non-commutative runs), which the
  perf prototype measured (linear, no cliff, fill-bound → region-local wins).

### Parked (each its own session)
- **MultiMesh / ADR-0040 integration** — how the unified fold consumes or refactors the
  5-bucket batching. User not yet comfortable with the ADR-0040 internals; needs dedicated
  time. Likely a refactor, not a bolt-on. This is the real remaining design risk.
- **Equal-z OT tie-break** — reverse-submission / HEAD-insertion asserted from psx-spx;
  confirm against the pcsx oracle (`effect-parity`) at build time. Low stakes (only bites at
  exact z ties).

### Immediate next action — the PERF PROTOTYPE
Ramp N ping-pong passes × {full-screen, small scissor rect} at 1280×960 on this hardware;
plot frame time. Purpose: find whether a cliff exists and **which axis** it's on —
- curves **diverge** → cost is fill/bandwidth-bound → region-local scissor wins, no cap ever.
- curves **overlay** → cost is per-pass fixed-overhead (draw/barrier/TBDR tile round-trip)
  → only *fewer passes* (grouping / cap) helps.
This single experiment decides whether the unified fold stands alone or needs the internal
batching/cap machinery. IF a cap is ever needed, it must be **top-N** (nearest folded exact,
deep tail approximated) — bottom-N drops the visible layers, random flickers under DEMI's own
randomness. Mirror the atom's pass; loop N dummy quads; read `Performance.get_monitor` /
frame time; headful.

---

## Perf prototype RESULTS (2026-07-19) — curves DIVERGE, no cliff, region-local wins

Ran the harness (`godot-learning/tools/compositor_prototype/proto_perf_fold{.gd,.glsl,
_effect.gd,.tscn}`, throwaway). K ping-pong raster passes between two owned
`A2B10G10R10_UNORM_PACK32` scratch attachments, blend OFF, mirroring the atom's RD
scaffolding. Two spatial conditions per K: full 1280×960 vs a 128×128 scissor rect
(`draw_list_begin` region arg). GPU time via `viewport_get_measured_render_time_gpu`,
median of 8–12 samples after warmup. **Hardware: RTX 5090 desktop GPU running the Mobile
renderer** (see caveat). Raw table: `captures/perf_fold_curve.txt`.

```
K       full_ms   scissor_ms   full−base   scissor−base   (base K=0 = 0.015 ms)
1        0.018      0.015         0.003        0.000
2        0.020      0.015         0.005        0.000
4        0.030      0.020         0.015        0.005
8        0.047      0.027         0.032        0.012
16       0.081      0.040         0.066        0.025
32       0.148      0.066         0.133        0.051
64       0.280      0.117         0.265        0.102
128      0.546      0.220         0.531        0.205
200      0.852      0.334         0.837        0.319
320      1.344      0.523         1.329        0.508
512      2.628      0.826         2.613        0.811
```

**Both curves are LINEAR in K with a near-zero intercept — no cliff, no knee, anywhere
from K=1 to 512.** Per-pass slopes (steady region):
- **full-screen ≈ 0.00415 ms/pass** (1.21 M px touched/pass).
- **scissor 128×128 ≈ 0.00159 ms/pass** (16 k px/pass — fill ≈ free, so this ≈ pure
  fixed per-pass overhead: draw_list setup + barrier/layout transition).
- ⇒ **fill component = full − scissor ≈ 0.00256 ms/pass.** Cross-check: full-screen
  reads+writes ~10 MB/pass; 512 passes = ~5 GB/frame ÷ ~1.8 TB/s ≈ 2.7 ms — matches the
  measured 2.6 ms. The full-screen tail is genuinely **bandwidth-bound**, confirming the
  fill reading.

### Which axis → the design fork it settles
The curves **DIVERGE** (2.6× per-pass separation at steady state), and the larger term is
**fill/bandwidth**, not fixed overhead (0.0026 vs 0.0016 ms/pass). Per the handoff's
pre-agreed decision rule, diverge → **fill-bound → region-local scissor wins; the internal
cap/grouping machinery is NOT on the critical path.**

- **Region-local fold = default architecture, now empirically justified.** Scissoring the
  fold to the polyphony blob cuts per-pass cost ~2.6× (the whole fill term). Fold the
  small region in region-sized ping-pong buffers, composite back once.
- **Cap / commutative-run grouping = DELETE from the critical path** (keep only as a
  documented optional optimization). Two reasons: (1) the cost is a smooth line, not a
  cliff — there's no N at which a pass suddenly gets expensive; (2) absolute magnitude is
  trivial at any realistic formation polyphony. A handful–dozens of prims full-screen is
  <0.1 ms; you need ~120 full-screen passes to reach 0.5 ms, ~240 for 1 ms. Grouping would
  only shave the *smaller* (fixed-overhead) term and only matters at pathological N.
  ⇒ **The unified display-space OT-ordered fold stands alone. DEMI's cap/top-N/grouping
  concepts are shelved** (top-N remains the policy *if* ever resurrected; not built now).

### Hardware caveat (the one real limitation of this run)
The port ships **desktop-with-Mobile-renderer** (Mobile chosen for the UNORM base-color
clamp per `psx-blend-clamp-forward-plus-fork`, *not* for actual mobile deployment), so
these desktop numbers **are the operative ones**. But note: the ~0.0016 ms/pass "fixed
overhead" measured here is Vulkan **draw_list/barrier** cost on an immediate-mode GPU — it
is **NOT** a true TBDR tile load/store round-trip. On a real tiled mobile GPU the
per-pass fixed cost would be dominated by full-attachment tile traffic (scaling with the
tiled area, heavier than a constant), so the divergence would grow *larger* and
region-local scissoring would help *more* (only tiles overlapping the scissor load/store).
The conclusions above (region-local wins, cap unneeded) therefore hold or strengthen on
TBDR; only re-measure if the port ever targets literal mobile hardware.

---

## Researched but NOT ratified — treat as still-open (superseded in part by prototype above)

Three of the six questions produced **no surviving verified claim**, partly genuine gaps
and partly the rate-limit storm (Vulkan spec pages also 403'd on re-fetch). Do not cite
this run as verification for these.

- **Q2 (OT-sort key / equal-z tie-breaking).** No principled answer found. The GTE/CUSTOM0
  → back-to-front sort key and PSX ordering-table insertion-order tie-break remain to be
  settled — best done empirically in the prototype (stable secondary key on the CPU-marshaled
  quad list).
- **Q3 (commutative-run grouping equivalence).** The algebraic question "does collapsing a
  run of same-mode commutative adds into one accumulate+clamp equal the per-prim clamped
  fold, and where does clamp break add/sub commutativity" got no ratified claim. This is a
  provable/unit-testable property — prove it against a CPU reference fold, don't research it.
- **Q6 Vulkan substrate (unratified but load-bearing).** The facts underpinning the whole
  color model — "sRGB fmt = linear-space add, plain UNORM = display-space add"; "UNORM
  clamps src/dst/factors to [0,1] pre-blend (free saturation), float never clamps";
  `VK_BLEND_OP_ADD = R_s·S + R_d·D` — were pulled from the primary Vulkan spec but landed
  **0-0 (abstain)**. They match existing project memory and are widely established, but this
  run did not independently ratify them.

---

## Implication for the plan

1. The **RD/Vulkan feasibility question is answered "yes — raster ping-pong on Mobile."**
   The atom's architecture is vindicated by primary sources; Q1/Q4/Q5 engine feasibility is
   not the risk.
2. The real unknowns are now **algorithmic (Q2 sort key, Q3 grouping equivalence)** — exactly
   what `prototype` settles byte-for-byte against a CPU reference, more definitively than more
   web search would.
3. **Q6 stays a measurement question** — "does combat *visibly* need the full display-space
   fold" is an `effect-parity` oracle A/B, not a doc question.

**Next step: `prototype`** — a throwaway N-atom ordered fold (3–4 overlapping quads, mixed
ABR modes, randomized submit order) verified byte-for-byte against a CPU reference. Then
`grilling` to pressure-test the design, then `wayfinder` to chart the build.

---

## Sources (primary, by angle)

- CompositorEffect / ping-pong: godot#98670, 4.6 compositor docs, RDPipelineDepthStencilState docs
- Mobile no-STORAGE: godot#96737, #108781, #99493, PR#71939, PR#72068
- Depth in CompositorEffect: godot#99493, #110629, #90148, paperman5/godot-depth-buffer-plugin
- Vulkan blend/format (unratified): docs.vulkan.org framebuffer chapter, Khronos chap27, VkBlendOp man page
- PSX model: psx-spx GPU, problemkaputt gpu-rendering-attributes, Psy-Q ordtbl.pdf, phoboslab wipeout rewrite
