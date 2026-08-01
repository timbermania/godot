# [Migrate · game] Pass A/C retarget + seed-texture wiring + run recipe + autopilot gate

Type: task
Status: open
Blocked by: 13, 14

## Question

Wire the retargeted seed/composite and get **first game-scene pixels on the new engine**. Detail:
`plans/03-game-migration-plan.md` D1 + D5.

- **Pass A (`FoldSurface.SeedPass`):** keep the linear→display seed shader, but write into a **game-owned RD
  texture** (the layer's `seed_texture`) instead of the magic-string scratch. Update it each frame; bind it on
  `res://assets/fold_layer.tres` so the engine (ticket 13) copies it into the target before Pass B.
- **Pass C (`FoldSurface.ResolvePass`):** read the target via `get_layer_texture(fold_layer)` (or
  `RenderSceneBuffersRD.get_texture("compositor_layer", str(id))` per Step-5 finding) instead of the magic-string
  scope; keep the display→linear + RGB555 + coverage-discard resolve.
- **Autopilot gate (D5):** `CompositorAutopilot` feature-detects
  `RenderingServer.has_method("is_compositor_layer_supported") && is_compositor_layer_supported()` instead of
  version-sniffing `≥4.8 && forward_plus`; stock-4.6 no-fold fallback unchanged.
- **Run recipe:** document how the game runs against the `godot-compositor-consume-material` dev build,
  **windowed, not `--headless`** (RenderingDevice required — analogue of `spike-fold-run-invocation`). Save as a
  memory note once verified.

**Done:** the fold renders in a real FFT scene on the new engine (add + sub + mix all correct against the
display-space seed), run windowed. This is the last step before the ticket-04 proof capture.

## Answer

<!-- filled on resolution -->
