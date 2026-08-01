# Game-migration plan — point the FFT fold at the `compositor_layer` primitive

**Resolves:** wayfinder ticket 03. **Game repo/branch:** `/home/curry/Repos/fft-monorepo-formation/godot-learning`
@ `feature/compositor-fold-migration` (cut from `import-godot-game`; fold verified present).
**Engine it targets:** `/home/curry/Repos/godot-compositor-consume-material` @ `feature/render-to-compositor`
(the general `compositor_layer` primitive; Steps 1–5 built, Steps 6/7 = tickets 11/12).
**Predecessor plan:** `.scratch/compositor-spike/plans/02-engine-implementation-plan.md`.

All game-side file references are in the migration worktree above.

---

## The frame model survives; only the seams move

The FFT fold's three-pass shape is **unchanged** by the migration:

| Pass | Timing | Job | Owner — today → after |
|---|---|---|---|
| A | `PRE_TRANSPARENT` | author the display-space scene seed | `FoldSurface.SeedPass` → **stays userland**, retargeted to write a game-owned texture bound as the layer's `TEXTURE` seed |
| B | engine (post-resolve, pre-`POST_TRANSPARENT`) | draw held-out members into the target, caller-ordered | engine `RENDER_LIST_COMPOSITOR_FOLD` → **engine `RENDER_LIST_COMPOSITOR_LAYER`** (Step 5, built) |
| C | `POST_TRANSPARENT` | resolve display→linear + RGB555 + coverage-discard, composite back | `FoldSurface.ResolvePass` → **stays userland**, reads `get_layer_texture(layer)` instead of the magic string |

What changes is **membership**, the **order key**, the **target handle**, and the **seed handoff** — not the algorithm.

---

## Decisions resolved by this ticket

### D1 — Seed architecture (the crux; grilled 2026-08-01)
- The PSX proof scene has **no non-folded transparent geometry behind the fold** → route all transparent
  through the fold, so the **opaque** scene is the complete seed `B`. No `SCENE_COLOR`, no new engine hook needed.
- Sub/mix modes are **on the critical path** (the honest shot exercises them), and sub/mix read `B` in place, so
  a **CLEAR seed is insufficient** — the seed must be the display-space scene.
- Mechanism = **bound `TEXTURE` seed** (plan-02 seed-source (i)). Pass A authors the display-space opaque scene
  into a game-owned texture; the engine copies it into the (separate, engine-owned) target just before Pass B.
  The target is never touched by transparent geometry, so the spike's composite-back clobber cannot recur here.
- **`SCENE_COLOR` is ruled out for the engine, not merely deferred:** the engine's scene buffer is linear HDR;
  the fold needs display-space UNORM. Seeding from `SCENE_COLOR` would force the engine to carry the
  linear→display transform = the exact FFT policy Step 6 forbids. It stays a **documented v2 extension** (the one
  seed userland cannot author itself, because there is no game callback in the resolve→Pass-B gap) — not built.
- **Generality principle (settled):** the "seed/composite at any point" flexibility is **already** the
  `CompositorEffect` callback API (Pass A/C at any hook, any math) composed with the resource's `stage` field
  (which moves Pass B, hence the seed point). Adding `seed_at`/`composite_at` knobs to the primitive would rebuild
  `CompositorEffect` as config and bake a blend policy into core = the upstream-eval's loudest 🔴. **Keep the
  primitive minimal; composite stays 100% userland.**

### D2 — Membership flip (material `render_mode` → per-instance property)
- **Shaders:** rename `render_mode …, compositor_fold` → `…, compositor_layer` in every fold shader:
  `assets/shaders/effect_fold_{add,sub,mix}.gdshader`, `crystal_fold.gdshader`, `tile_decal_fold.gdshader`,
  `cursor_fold_{add,sub,mix}.gdshader`, `effect_callback_fold.gdshader`.
- **Per-instance opt-in:** every fold carrier now also sets `render_layer` + `render_layer_order`. Add both to
  `Fold.add()` (`src/effects/Fold.gd`) and `EngineFoldCompositor`'s per-frame carrier build
  (`src/effects/EngineFoldCompositor.gd`). A `MultiMeshInstance3D` is a `GeometryInstance3D`, so one carrier (=one
  OT run) carries one `render_layer_order` — **granularity matches today's one-`sorting_offset`-per-carrier exactly.**
- **The shared layer resource:** one `res://assets/fold_layer.tres` (`CompositorRenderLayer`) referenced by every
  fold carrier → one partition → Step 5's one-partition fast path. Fields: `format = A2B10G10R10_UNORM_PACK32`,
  `seed_source = TEXTURE`, `seed_texture = <the Pass-A game-owned texture>`, `stage = POST_TRANSPARENT`.

### D3 — Order key: float `sorting_offset` → int32 `render_layer_order`
- `DepthMode.sorting_offset_for(order_z, rank) = round(order_z / 0.19) + rank·1e-4` (float) becomes
  **`render_layer_order_for(order_z, rank) = round(order_z / 0.19) · RANK_STRIDE + rank`** (int32).
- Same key semantics: primary = OT depth bucket (already integer), secondary = `rank` (submission order within
  bucket) — the **shared scale** that lets independent producers (pool carriers vs. direct `Fold.add` carriers)
  interleave by depth. `RANK_STRIDE` (e.g. `1 << 10`) bounds per-bucket rank; bucket range (±~300 from
  `FIXED_BACK` z=−50 / near-front) × 1024 fits int32 with vast headroom. No float32-ULP cliff, no NaN
  canonicalization (an int can't be NaN) — this is exactly why the engine chose int (plan-02 default #1).
- `OTDepthPrimOrder.order()` is **unchanged** — it still produces the ordered run list; only the encoding of each
  run's key into the instance changes.

### D4 — Coverage becomes the shader's explicit responsibility
- The engine (ticket 11) **drops** the forced coverage-α (ADD/ONE/ONE) override. Pass A seeds α=0 and Pass C
  discards coverage≈0, so the coverage channel must survive. Each fold shader must **write coverage into ALPHA for
  covered fragments and declare an alpha blend that accumulates it** (the behaviour the engine used to force).
  This is verified by Pass C still masking correctly — a first-class migration check for ticket 14, not an
  afterthought.

### D5 — Engine binary selection + capability gate
- The game runs against the `godot-compositor-consume-material` dev build, **windowed, not `--headless`** (a
  RenderingDevice is required — same constraint as `spike-fold-run-invocation`). A run recipe (analogue of that
  memory note) is produced in ticket 15.
- `CompositorAutopilot` (`src/effects/CompositorAutopilot.gd`) switches from version-sniffing (`≥4.8 &&
  forward_plus`) to feature-detect: `RenderingServer.has_method("is_compositor_layer_supported") &&
  RenderingServer.is_compositor_layer_supported()` (built by ticket 12/Step 7). Cleaner, and it degrades to the
  stock-4.6 no-fold fallback unchanged.

### D6 — Test story (what locks the new routing)
- **Update** (`compositor_fold`→`compositor_layer`, plus assert each carrier sets `render_layer` + a valid int
  `render_layer_order`): `CallbackFoldRoutingTest`, `TileOverlayCompositorTest`, `CrystalSpriteCompositorTest`,
  `TileCursorCompositorTest`, `FeedbackHudFoldRoutingTest`, `FormationFoldRoutingTest`.
- **Update encoding** in `DepthModeTest`: `sorting_offset_for` → `render_layer_order_for` (int; assert bucket
  primary / rank secondary / shared-scale interleave; add negative-bucket ordering across a `RANK_STRIDE`
  boundary).
- **Unchanged:** `OTDepthPrimOrderTest` (ordering algorithm untouched), `UnifiedPrimStagerTest`.
- These are headless GDScript tests — the membership/rename/re-key migration (ticket 14) is fully lockable
  **without** a GPU or the new engine binary.

---

## The critical-path insight

Sub/mix on the proof's critical path ⇒ the display-space seed is required ⇒ the engine's **`TEXTURE` seed source
must ship before the game can render its real fold**. Step 5 shipped CLEAR only (TEXTURE warns and falls back).
So this migration graduates **one engine ticket that blocks the game's rendering-verified tickets** — it is not a
purely game-side effort. `TEXTURE` seed is *also* a clean, general, minimal engine feature (not FFT-specific), so
the prerequisite and the upstream surface are the same work.

---

## Ticket graph graduated from this plan

- **13 — Build · `TEXTURE` seed source** (engine, task). Blocked by 10. Critical path. Replace Step-5
  warn-and-fallback-to-CLEAR with a real copy of the layer's bound `seed_texture` into the target before Pass B;
  add `seed_texture : Texture2D` to `CompositorRenderLayer`; the seed must accept a **live/per-frame RD-backed**
  texture (the game updates it each frame in Pass A), not just a static import.
- **14 — Migrate · membership flip + int order key + shader rename + routing tests** (game, task). Blocked by 07,
  08, 09 (all resolved) — writable and headless-verifiable **now**, independent of 13. D2 + D3 + D4 + D6.
- **15 — Migrate · Pass A/C retarget + seed-texture wiring + run recipe + autopilot gate** (game, task). Blocked
  by 13 (needs `TEXTURE` seed) + 14 (needs membership). D1 wiring + D5. First game-scene pixels on the new engine.

The proof **capture** graduates separately from ticket 04 (define the proof scene), once 15 lands.
