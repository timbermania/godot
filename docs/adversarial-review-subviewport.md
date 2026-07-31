# Adversarial review — does the "compositor render layer" survive the SubViewport attack?

**Written:** 2026-07-31 · **Reviewer stance:** skeptical Godot rendering maintainer, mandate to *reject*.
**Target:** `docs/proposal-draft-compositor-render-layer.md`.
**Method:** the four ADR-0001 SubViewport claims were re-verified against primary `Viewport` / `SubViewport` /
`World3D` / `Camera3D` / renderer source (3 parallel source-dive agents, all citations read from the file at the
cited line on branch `feature/render-to-compositor`, stock Godot 4.8-dev @ `4e8c061c9b`). This supersedes nothing;
it is the attack the proposal must survive before it is worth a maintainer's time.

---

## Verdict up front

> **The proposal survives — but only on a much narrower base than it is pitched on, and its headline argument is a
> strawman against the workaround that matters.** A SubViewport *runs the real material*, so the proposal's loudest
> pain ("~940 lines of `.gdshaderinc` hand-ported to GLSL and byte-synced forever") **evaporates against a
> SubViewport** — that pain is only real against the *CompositorEffect-compute* workaround. The proposal therefore
> cannot lean on it and must justify itself *purely* on the three things a SubViewport provably cannot do:
> **(1) occlude the held-out layer against the main scene's opaque depth, (2) draw it in a caller-controlled,
> depth-decoupled order, and (3) seed/accumulate it in-frame in coordination with the main viewport's
> `CompositorEffect`.** Those three are real, verified, and unreachable. Everything else the proposal claims —
> object-isolated post-processing, the "no GLSL" headline, the cost argument — is either SubViewport-reachable,
> already-served, or convenience. **Recommendation: narrow the proposal to that verified core (headline it on
> shared-scene-depth occlusion), stop over-claiming demand, and reconcile the buffer/format surface with #7916
> instead of rebuilding it.**

---

## Q1 — The SubViewport capability table (the crux)

Each row is a capability the proposal implicitly or explicitly claims only the engine can deliver. Verdict is
against a SubViewport workaround (held-out meshes in a `SubViewport`, its texture consumed by a `CompositorEffect`).

| # | Capability | SubViewport verdict | Primary-source evidence |
|---|---|---|---|
| A | **Shade held-out geometry through its real Godot material** (no GLSL re-port) | ✅ **CAN** | A SubViewport renders the real scene through the normal forward pipeline. **This nullifies the proposal's headline pain vs SubViewport.** |
| B | **Occlude the held-out layer against the *main scene's* opaque depth** | ❌ **CANNOT** (only via 2× occluder duplication + color-masking) | World3D shares only the scenario/object-set, never pixels: `world_3d.cpp:190` (scenario), `viewport.cpp:4907` (`viewport_set_scenario` is the *only* render wiring `set_world_3d` does). Depth is per-render-target: `renderer_viewport.cpp:287,995`, `render_scene_buffers_rd.cpp:192`. The one depth-swap facility (`render_target_set_override`, `texture_storage.cpp:4540`) is **XR-private and unbound** — not reachable from script. |
| C | **Draw the layer in caller-controlled, depth-decoupled order** | ❌ **CANNOT** (fake = N full renders) | Alpha sorts by reverse camera depth: `render_forward_clustered.cpp:1918`, comparator `render_forward_clustered.h:722-726`; `depth = camera_distance − sorting_offset` (`:962/:967`). `sorting_offset` is a depth *bias*, not an order key; `render_priority` is a coarse *per-material* bucket that still depth-sorts within. **No pure caller-order key exists in stock Godot.** |
| D | **Seed the target (effect writes initial contents) + accumulate the layer onto it, coordinated with the *main* viewport's compositor** | ❌ **CANNOT** | Not a clear-mode issue (`CLEAR_MODE_NEVER` exists, `viewport.h:904-908`). Two hard blockers: (a) no API injects an externally-authored frame-start seed into a viewport's color attachment — the 3D pass LOADs the *internal* `RB_TEX_COLOR` (`render_scene_buffers_rd.cpp:186`; LOAD gate `render_forward_clustered.cpp:2016,2225`), not the addressable render-target texture; (b) sub-viewports render **before** the main viewport, so a main-viewport effect runs *after* the SubViewport is already done: topological order children-first `renderer_viewport.cpp:98-134,862`. The data dependency is backwards. |
| E | **Single-pass cost** (no second full-frame resolve/tonemap, no texture round-trip) | ⚠️ **CAN-BUT-DEGRADED** | A SubViewport is a *complete independent scene render*: own cull `renderer_scene_cull.cpp:3461`, own MSAA resolve `render_forward_clustered.cpp:2310`, own tonemap/post `:2551`, plus a `ViewportTexture` sample-back. The in-frame pass avoids those. **But honestly:** the saving is the *second full-frame resolve/tonemap + RT round-trip + duplicate cull*, **not** triangle work (a SubViewport holding only the few held-out objects draws only a few objects). Convenience/perf, not new capability. |
| F | **Object-isolated post-processing** (#7849 / #2196 — "run an effect on *just these* objects") | ✅ **CAN** (largely) | Put the marked objects in a SubViewport, post-process its texture. This is the proposal's cited "best mass-appeal fit" — and it is broadly SubViewport-reachable, so it is **not** load-bearing for the proposal. |

**Reading the table.** The proposal survives on **B, C, D** — and *only* B, C, D. Those are the capabilities a
SubViewport provably cannot reach, each with engine evidence. A, E, and F — the GLSL-pain headline, the cost
argument, and the object-isolated-post use case — are all reachable or convenience, and the proposal must stop
leaning on them when arguing against SubViewport.

### The intersection that is genuinely new

B ∧ C ∧ D is a single, coherent capability: **a held-out layer that (B) occludes against the real scene, (C) in
an author-chosen order, (D) seeded by and composited within one frame by the effect that owns it.** That
intersection is exactly the PSX-fold, and it is exactly what a SubViewport's clear-then-render-in-isolation,
own-depth, depth-sorted, render-first architecture cannot express. *That* is the honest proposal. The moment the
pitch broadens past this intersection (to "object-isolated post" or "material→texture without a SubViewport"), the
SubViewport workaround reappears and the justification weakens.

---

## Q2 — Is the demand enough to justify permanent core surface? (skeptical)

The demand research is honest, but its own numbers do not support the "several hundred combined upvotes" framing
once you subtract what is *borrowed*, *shipped*, or *reachable*:

- **#7916 (193)** is the *ally*, not demand for this. Its author explicitly dismissed the transparent case
  ("not much of a point… transparency is sorted back-to-front in a single pass"). Those 193 upvoted the *opaque*
  compositor, whose author waved off the exact thing this proposal builds. Citing 193 is mindshare, not demand.
- **#7174 (95)** — **shipped in 4.5** (stencil Outline/X-Ray). Dead as demand.
- **#7379 (136)** — CLOSED, 2D-canvas-shaped; "mesh → texture" is already SubViewport-served.
- **#798 (94)** — wants *engine* G-buffers (depth/normal/AOV), which the research itself says must not be conflated
  with a caller-authored layer.
- **#644 (99)** — the scriptable-pipeline umbrella; this is at most a slice of it, not the ask.

What is left that this *uniquely and newly* serves: **#7849 (88) + #2196 (55)** object-isolated post — **and per
row F that is largely SubViewport-reachable** — plus the genuinely small ordering asks **#3986 (26) + #11251 (17)
≈ 40**, where #11251 wants the *opposite* (order-independence). Net: the demand is **diffuse and proxy-heavy**. No
single proposal asks for this primitive; the one sizable open ask it fits (#7849) is mostly reachable today. That
is a thin basis for permanent hot-path core surface — unless the pitch is narrowed to the B∧C∧D intersection,
where demand is small (NPR ordered transparency + the fold's own niche) but at least *uncontested and unreachable*.

**Skeptic's conclusion:** the mass-appeal story does not hold; the *capability* story (B∧C∧D) does. Pitch the
latter, honestly small, or expect "closed as diffuse / covered by #7916 + SubViewport."

---

## Q3 — Genuinely new capability vs. mere convenience

- **New capability (only-the-engine):** B (shared-scene-depth occlusion), C (caller-order), D (seed-LOAD in-frame).
  Verified unreachable.
- **Convenience:** A (avoids GLSL re-port — but only vs the *compute* workaround, not vs SubViewport) and E
  (avoids a second resolve/round-trip — real but modest, not "a whole scene render's drawing").
- **Not the proposal's to claim:** F (object-isolated post) and "material→texture" — SubViewport-reachable.

The proposal repeatedly frames A (the GLSL wall) as the core justification. Against the workaround that actually
matters — SubViewport — A is a **strawman**. The engine wall the proposal genuinely dissolves is the *compositor
effect* wall (an effect cannot invoke a `ShaderMaterial` — confirmed, `renderer_scene_render_rd.cpp:298-317`), but
a developer who just wants the real material in a buffer reaches for a SubViewport, not a compute re-port. So A
only bites a developer who *also* needs B/C/D badly enough to have rejected SubViewport first — i.e. A is
downstream of B∧C∧D, not an independent argument.

---

## Q4 — Is the capability unlocked with minimum disruption?

Three concerns, harshest reading:

1. **It duplicates #7916's surface.** The typed `CompositorRenderTarget`, the enumerated formats, and the
   `CUSTOM_BUFFER0..N` aux-output vocabulary are all things #7916 already specifies. Filing standalone risks
   building a **parallel buffer-declaration system**. The lighter seam: contribute the *transparent / held-out /
   caller-ordered list* on top of #7916's buffer infra, so the genuinely new code shrinks to the one list + the
   instance opt-in. As a standalone, it is not minimal.

2. **"Mobile mirrors" is hand-wavy.** Research §6 (`render_forward_mobile.cpp:887-903`) shows Mobile blends
   transparency inside a *subpass chain that ends in a display-space tonemap*, and any pre/post-transparent effect
   already forces Mobile off its subpass fast-path. A post-resolve deferred layer on Mobile is single-sample *and*
   in a different color space than Forward+ — genuinely different, not a mirror. The proposal's "Forward+ shown;
   Mobile mirrors" understates real per-renderer work.

3. **The hot-path touch is irreducible but small.** Even in the tidier "deferred holdout" framing, you still need a
   fill-branch to *hold instances out* of the normal opaque/alpha lists (`_fill_render_list`, the hottest loop) and
   a per-instance property read. That is unavoidable for a holdout primitive; it is bounded, but it is permanent
   ABI in the most-used renderer. Acceptable *if* B∧C∧D justify it; not obviously so on the diffuse demand.

---

## What survives, and the recommendation

**Survives (verified, unreachable by SubViewport):**
- **B — shared-scene-depth occlusion of the held-out layer.** The single strongest pillar; make it the headline.
- **C — caller-ordered, depth-decoupled draw.** Real, low-demand, but genuinely absent from stock Godot.
- **D — in-frame seed-LOAD + accumulate coordinated with the main compositor.** Real, niche.

**Does not survive as justification:**
- **A — the "940 lines of GLSL" headline** — strawman vs SubViewport; keep it *only* as the argument against the
  compositor-compute workaround, clearly scoped.
- **F — object-isolated post-processing** — SubViewport-reachable; demote from "best fit" to "adjacent."
- **The demand tally** — proxy-heavy; drop the "several hundred upvotes" framing.
- **E — cost** — real but modest; state it honestly (a second resolve/tonemap + round-trip, not a second scene's
  drawing), do not headline it.

**Recommendation.** Do **not** reject outright — the B∧C∧D intersection is a real engine-only capability and the
PSX-fold is a legitimate motivating client. But the current draft over-claims and would invite a
"closed as diffuse / SubViewport-covered / see #7916" bounce. **Narrow and re-headline it:**

1. Lead with **shared-scene-depth occlusion of a caller-ordered, seeded held-out layer** (B∧C∧D) as *the* capability
   SubViewport structurally cannot express — with this doc's evidence table.
2. **Scope the GLSL-pain argument** to the compositor-compute workaround only; concede SubViewport runs the real
   material.
3. **Concede F** (object-isolated post is SubViewport-reachable) and **demote the demand numbers** to honest,
   small, uncontested asks (NPR ordered transparency + the fold).
4. **Reconcile with #7916's buffer/format/aux-output surface** rather than rebuilding it; pitch as its
   transparent/held-out counterpart *reusing* its infra, minimizing new code (Q4).
5. If the rendering team still declines the seam, the **fork remains the correct fallback** (ADR-0001) — but the
   proposal, narrowed this way, is honest enough to be worth asking first.

### The one-line honest pitch

> *A `SubViewport` can render your held-out geometry through its real material — but it renders into its own depth
> buffer, in depth-sorted order, cleared and in isolation, one pass too early to be seeded by your compositor. This
> primitive is for the case where you need that geometry occluded by the real scene, in your order, seeded and
> composited in-frame by the effect that owns it. That case — not "object-isolated post" — is what only the engine
> can do.*
