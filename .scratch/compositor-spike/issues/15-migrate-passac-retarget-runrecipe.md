# [Migrate · game] Pass A/C retarget + seed-texture wiring + run recipe + autopilot gate

Type: task
Status: CLOSED (resolved 2026-08-01, Aaron Curry)
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

**RESOLVED — the fold renders in a real FFT scene on the new engine; add + sub + mix all correct
against the display-space seed, verified windowed.** Game repo `fft-monorepo-formation/godot-learning`
@ `feature/compositor-fold-migration`, 5 files (+127/−53), uncommitted.

**Wiring (D1 + D5):**
- **Pass A (`FoldSurface.SeedPass`):** authors the display-space seed into a **game-owned RD texture**
  wrapped in a `Texture2DRD`, bound once as `Fold.FOLD_LAYER.seed_texture` (rid updated in place per
  frame). Replaces the old `rb.create_texture("compositor_fold","color")` magic-string scratch. The
  engine (ticket 13) copies it into the held-out target before Pass B, then draws carriers over it with LOAD.
- **Pass C (`FoldSurface.ResolvePass`):** reads the engine-owned target via `get_layer_texture(Fold.FOLD_LAYER)`
  (declares `render_layers=[Fold.FOLD_LAYER]`); resolve math (display→linear + RGB555 + coverage gate) unchanged.
- **Autopilot (D5):** `CompositorAutopilot` now feature-detects `RenderingServer.has_method("is_compositor_layer_supported")
  && is_compositor_layer_supported()` instead of version-sniffing ≥4.8/forward_plus. Verified: prints ACTIVE.
- **Cleanup:** `SeedPass` frees its RD texture + unbinds `seed_texture` on `NOTIFICATION_PREDELETE`.

**Two implementation-forced corrections surfaced by verification:**
1. **Layer format A2B10G10R10 → RGBA8** (`fold_layer.tres` format 2→1). A `Texture2DRD` can only wrap an RD
   format with an `Image::Format` equivalent; A2B10G10R10 has none (`texture_rd_create` → "Unsupported image
   format"). RGBA8_UNORM matches the engine's `FORMAT_RGBA8→R8G8B8A8_UNORM` mapping, is UNORM (preserves PSX
   additive saturation, unlike a float format), gives 8-bit (not 2-bit) coverage alpha, and is fidelity-equivalent
   after Pass C's RGB555 quantize. Plan-02/ticket-14's A2B10G10R10 was unusable as a game-authored seed.
2. **Subtractive coverage fix (seed-alpha baseline).** Verification caught that **subtractive folds vanished**:
   Godot's `blend_sub` uses `alpha_blend_op=REVERSE_SUBTRACT` (material_storage.cpp `BLEND_MODE_SUB`), so a sub
   carrier drives coverage-alpha to 0 → Pass C's `s.a<=0.001` discard dropped it. **This corrects ticket 14's D4
   and the `effect_fold_sub` comment, both of which wrongly claimed blend_sub keeps alpha additive.** The old
   spike hid it by forcing coverage-α in the engine; ticket 11 stripped that. **Fix chosen = userland (user:
   "most palatable upstream") — keeps the engine minimal, composite stays 100% userland:** `foldsurface_seed.glsl`
   seeds coverage-alpha to a **0.5 baseline** (was 0), `foldsurface_resolve.glsl` gates on **deviation**
   (`abs(s.a-0.5)<=0.1 discard`). add→α1.0, mix→0.75, sub→0.0 all deviate → composited; untouched stays 0.5 →
   discarded; RGB path untouched so the ticket-04 additive proof is byte-identical. See memory
   `compositor-fold-coverage-baseline`.

**Verification (windowed, decisive):** new controlled guard `tools/probe_ticket15_roundtrip.gd -- add|sub|mix`
— a gray seed + one minimal `compositor_layer` carrier over the right half, driven by the PRODUCTION FoldSurface;
all three PASS (add right=0.808 brighter; sub right=0.192 darker-but->0 = subtracted from a REAL seed; mix
right=0.388; untouched left=0.498 scene). Real Formation scene (`probe_formation_fold_capture.tscn`) renders
clean, no regression. Engine rebuilt to include ticket 13's seed-copy.

**Run recipe (D5) → memory `compositor-fold-game-run-invocation`:** windowed + `--rendering-method forward_plus`;
`.glsl` edits need a `--editor --quit` reimport first (the `-s` path uses stale cached SPIRV — cost a debug cycle).

**Handoff:** ticket 04's DEMI2 A/B proof capture is now unblocked (runs from the game repo). Game changes are
**uncommitted** — user to commit. The `probe_ticket15_roundtrip.gd` guard is worth keeping (add/sub/mix regression).
