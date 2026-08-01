# Handoff — DEMI2 (E046) A/B fold-color proof on the new `compositor_layer` engine

**This is the champion-earning proof for the #7916 sounding** — the answer to wayfinder ticket 04
("Define the proof scene and capture method"). Run from the game repo, which holds the DEMI2 context.

## What the proof is

Re-run the **DEMI2 single-held-particle `with − without` fold-color audit**, but against the **new
material-side `compositor_layer` primitive** (engine branch `feature/render-to-compositor` in
`~/Repos/godot-compositor-consume-material`), on the migrated game. The `with − without` diff on flat
mid-gray 128 isolates the fold's exact per-channel contribution — proving the migrated primitive
renders material-shaded held-out geometry that composites **faithfully**, the capability #7916 today
cannot claim.

## Why the measured diff, not a combat screenshot

- The capability's hardest-to-fake claim is **B**: *material-shaded held-out geometry contributes the
  correct pixels through the compositor.* The A/B diff nails that numerically (per-channel,
  background-cancelled) — far stronger to a skeptic than any screenshot.
- **C** (occlusion by resolved scene depth) and **D** (caller-order) are already proven at the *engine*
  level by RD readback: ticket 09 (ordering `{5,5,-2,0}` → `-2 0 5 5`, member held out) and ticket 10
  (visible → drawn `255,0,0,255`; occluded → transparent `0,0,0,0`). The game proof doesn't re-show them;
  it proves the end-to-end **color** path through the migrated shaders.

## Preconditions (must hold before the new-engine run)

1. **Engine built:** `~/Repos/godot-compositor-consume-material` @ `feature/render-to-compositor`,
   `scons platform=linuxbsd target=editor dev_build=yes -j24` → `bin/godot.linuxbsd.editor.dev.x86_64`.
2. **Game migrated** onto `compositor_layer` (wayfinder tickets **14 + 15**): the DEMI2 particle's
   `effect_fold_add` shader must declare `render_mode …, compositor_layer` and its carrier set
   `render_layer` / `render_layer_order`. Until then the particle falls back to no-fold on the new engine
   (autopilot gates on `is_compositor_layer_supported()`).
3. **Seed:** the seq-2 magenta target is **additive**, which does not read `B` in place → **CLEAR seed
   suffices**. The `TEXTURE` seed (ticket 13) is only required if the proof set is later extended to
   sub/mix particles.

> Optional now: capture the **stock baseline** on `~/Repos/fft-monorepo-game/godot-learning` @
> `import-godot-game` (the original 2026-07-31 run's engine) to re-lock the oracle reference before
> migration lands, so the post-migration port frame has a same-rig comparand.

## The rig (unchanged — port side)

- **Repo (new-engine run):** `~/Repos/fft-monorepo-formation/godot-learning` @
  `feature/compositor-fold-migration`.
- **Script:** `tools/proto_single_held_particle.gd` — throwaway `SceneTree`, boots the real EffectViewer
  sized to the 256×240 virtual framebuffer, **zero production edits**.
- **Target:** emitter **2** → sequence **2**, the **additive magenta** particle, `--fs=26` (36×36, the
  biggest — white-clamped core + magenta halo), held at **age 7**. Emitter-bound so port + oracle isolate
  the same thing; magenta exercises the per-channel color math better than a white blob. (Blend map: seq2
  is additive — the old "e2=subtractive / e4=gold" note was wrong; blend, not color, is the axis.)
- **Mechanism:** `play_effect(id, parked=true)` + `seek(N)` (ADR-0070 re-pump then HOLD) →
  `get_active_particles()` → pick one survivor → `eff.set_process(false)` →
  `sprite_renderer.update_particles([survivor])` for **WITH**, `update_particles([])` for **WITHOUT**.
- **Background:** flat mid-gray **128** (headroom both directions; additive can't clip to 1.0, subtractive
  can't clip to 0.0). With flat 128, `with − without = WITH − 128` per channel. Units `.visible=false` as
  anchors; dither off.
- **Gotcha:** override `--gamma=` / `--brightness=` at **draw** time, not `_initialize` (the PSXDisplay
  autoload re-applies 1.4 / 2.2 after init). The visible sky is a static ScreenBackground quad → it cancels
  in the diff; **always measure the diff, not raw WITH**.

## Run (windowed — RenderingDevice required, NOT `--headless`)

```
WAYLAND_DISPLAY=wayland-1 DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/1000 \
  ~/Repos/godot-compositor-consume-material/bin/godot.linuxbsd.editor.dev.x86_64 \
  --path ~/Repos/fft-monorepo-formation/godot-learning \
  tools/proto_single_held_particle.gd --fs=26
```

Capture WITH and WITHOUT, subtract. Headless uses the dummy RenderingServer → no RenderingDevice → no GPU
readback (silent no-op). See memory note `spike-fold-run-invocation`.

## Oracle (ground truth to diff against)

Stock/PCSX reference from the original DEMI2 run — `research/working_documents/demi2_fold_audit/oracle_rig.sh`
(headless PCSX over port 8080, effect-editor Preview Mode, single held additive particle on flat mid-gray,
overhead cam). Gotchas: always `PCSX_AGENT_NO_SCREENSHOT=1` (auto-shot wedges the single-threaded server);
effect-editor savestates are raw/uncompressed — `PCSX.loadSaveState` direct (zReader hard-crashes); inline
`ee_load_session`'s parse body synchronously (it wedges otherwise). Reuse unchanged.

## Pass criteria

- `WITH − WITHOUT` cleanly isolates the particle's contribution on the new-engine build.
- Per-channel **R:G:B ratio matches the oracle** (no overbright / ratio shift) — the migrated
  `compositor_layer` fold reproduces the stock contribution. Reference from the original audit:
  gamma ∈ {1.0, 1.4, 3.0} → byte-identical; brightness ∈ {1.0, 2.2, 4.0} → energy 21.7k / 45.4k / 64.6k
  (clean scalar gain). Residual port≠oracle deltas expected only from camera framing + SHP fs26 palette
  decode, **not** the fold.
- Confirm the migrated `effect_fold_{add,sub,mix}` shaders carry the audit's **dead-`pow` removal** (the
  live math must be `ALBEDO = raw_texel · COLOR.rgb · psx_brightness`, a pure scalar multiply). The separate
  `formation_orb_rim_fold` / `cursor_fold` double-brightness fixes are out of scope for this particle verdict.

## Storage + sounding linkage

- Store the new-engine WITH / WITHOUT / diff PNGs + the energy table under
  `research/working_documents/demi2_fold_audit/`, tagged `new-engine` vs the original stock run.
- On a **faithful** result, restore the "working fork proves the capability" line to
  `~/Repos/godot-compositor-consume-material/docs/sounding-7916-comment-draft.md` (withheld until the fold
  actually renders) and link the diff artifact. **Do NOT post to `godotengine`** — the user posts it themselves.
