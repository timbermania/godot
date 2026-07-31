# Game-migration plan: point the FFT game at the new engine and map the fold onto the new primitive

Type: grilling
Status: open
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

<!-- filled on resolution -->
