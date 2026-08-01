# Game-migration plan: point the FFT game at the new engine and map the fold onto the new primitive

Type: grilling
Status: resolved
Assignee: Aaron Curry (claimed 2026-08-01)
Blocked by: 02

## Question

With the new primitive's API shape decided (ticket 02), plan how the FFT game
(`/home/curry/Repos/fft-monorepo-game/godot-learning`) moves off the old fork's Pass-A/B/C mechanism
onto it.

Resolve:

1. **Which worktree/branch — DONE (2026-07-31):** the `fft-monorepo-formation` worktree is prepped on
   branch **`feature/compositor-fold-migration`** (cut from `import-godot-game`; fold system verified
   present). Prior `formation-screen` untracked WIP was discarded per user; the 3 substantive research
   docs salvaged to `.scratch/compositor-spike/research/wip-from-formation/` as inputs here. `-game` and
   `-compositor` left for other agents. Remaining to resolve: how the game selects the new engine binary
   (project.godot feature pin, run script, `/usr/bin/godot` vs the new `godot-compositor-consume-material`
   build path).
2. **Producer-by-producer mapping** onto the new API vs the old fork:
   - `FoldSurface` (Pass A seed / Pass C resolve scratch lifecycle) — does the engine now own this?
   - `EngineFoldCompositor` (per-frame carrier rebuild from `EffectMultiMeshPool`).
   - `DepthMode.sorting_offset_for()` / `OTDepthPrimOrder` — replaced by the caller-order key, or kept?
   - The monomorphic `*_fold` shaders' `render_mode` / material membership declaration.
   - `CompositorAutopilot` fork-detection + the stock-4.6 no-fold fallback path.
3. **Test story** — which existing headless routing tests (`CallbackFoldRoutingTest`,
   `TileOverlayCompositorTest`, `DepthModeTest`, …) must be updated, and what locks the new routing.

Output a migration plan (the game-side edits graduate from the fog as task tickets after this resolves).

## Answer

**Resolved 2026-08-01 (grilling).** Full migration plan:
`.scratch/compositor-spike/plans/03-game-migration-plan.md`.

The FFT fold's three-pass shape survives; only the seams move. Decisions:

- **Seed = bound `TEXTURE` (the crux).** Sub/mix are on the proof's critical path and read `B` in place, so
  CLEAR is insufficient — the seed must be the display-space scene. The PSX scene has **no non-folded transparent
  behind the fold**, so the opaque scene is the complete seed; Pass A authors it into a game-owned texture bound as
  the layer's `TEXTURE` seed, and the engine copies it into the separate engine-owned target before Pass B (the
  spike's composite-back clobber cannot recur — the target is untouched by transparent geometry).
- **`SCENE_COLOR` ruled OUT of the engine** (not just deferred): the scene buffer is linear HDR, the fold needs
  display-space UNORM; engine-side `SCENE_COLOR` would force the linear→display transform into core = the FFT
  policy Step 6 forbids. Documented as a userland-impossible v2 extension; not built.
- **"Seed/composite at any point" flexibility = resist.** It already exists as `CompositorEffect` (Pass A/C at any
  hook, any math) composed with the resource's `stage` field. Configurable insertion-point knobs would rebuild
  `CompositorEffect` as config + bake a blend policy into core = the eval's loudest 🔴. Composite stays 100% userland.
- **Membership flip:** shaders rename `render_mode compositor_fold`→`compositor_layer`; every fold carrier
  (`Fold.add`, `EngineFoldCompositor`) also sets `render_layer` (one shared `fold_layer.tres`) + `render_layer_order`.
- **Order key:** float `DepthMode.sorting_offset_for` → int32 `render_layer_order_for(order_z, rank) =
  round(order_z/0.19)·RANK_STRIDE + rank`. Same bucket-primary/rank-secondary shared scale; no ULP cliff.
  `OTDepthPrimOrder` unchanged.
- **Coverage** becomes the shader's explicit ALPHA responsibility (engine drops the forced ADD/ONE/ONE).
- **Binary/gate:** run windowed against the `godot-compositor-consume-material` dev build; `CompositorAutopilot`
  feature-detects `is_compositor_layer_supported()` instead of version-sniffing.
- **Tests:** the routing/`DepthMode` tests update to the new render_mode + int key and lock the migration
  **headless**, no GPU needed.

**Critical-path consequence:** sub/mix ⇒ display-space seed ⇒ the engine's `TEXTURE` seed source must ship before
the game renders. This migration therefore graduates **one engine ticket (13) that blocks the game's
render-verified ticket (15)** — it is not purely game-side.

**Graduated tickets:** 13 (engine `TEXTURE` seed, blocks 15), 14 (game membership/order-key/rename/tests —
writable now), 15 (game Pass A/C retarget + wiring + run recipe, blocked by 13+14). Proof capture graduates
separately from ticket 04.
