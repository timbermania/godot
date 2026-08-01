# Define the proof scene and capture method

Type: grilling
Status: closed
Assignee: Aaron Curry
Blocked by: —

## Question

The destination's proof is "the fold rendering in a real FFT scene, captured." Pin down exactly what
that means so "done" is unambiguous — this is independent of engine internals and can be decided now.

Resolve:

1. **Which scene** is the proof — `res://assets/scenes/GPUArena.tscn` (the current main scene), or a
   specific combat/scenario scene that best shows the fold (blue Move / red Attack tiles, crystal
   sprites, callbacks, damage-number HUD all blending in display space)?
2. **What look must be visibly correct** — the specific fold artifacts that prove B∧C∧D: the
   PSX-faithful clamped additive blend, held-out geometry occluded by opaque scene, caller-ordered
   layering. What would a skeptical #7916 reviewer need to see to believe the capability exists?
3. **Capture method** — screenshot vs short video; via `run` / `verify`; windowed with a real Vulkan
   device (NOT `--headless`). Where the artifact is stored and how it's linked into the sounding draft.

Use `grilling` (and `prototype` if a rough shot list helps). Actually capturing the shot is fog that
graduates once the game runs on the new engine (tickets 02 + 03).

## Answer

**Resolved (grilling). The proof is the DEMI2 (E046) single-held-particle A/B fold-color audit,
re-run on the new `compositor_layer` engine — NOT a combat-scene screenshot.**

1. **Scene/proof:** the DEMI2 rig (`tools/proto_single_held_particle.gd`, EffectViewer at 256×240,
   emitter 2 → seq 2 additive magenta particle, `--fs=26`, held age 7), driven from the game repo.
   The `with − without` diff on flat mid-gray 128 isolates the fold's exact per-channel contribution.
   GPUArena/ScenarioPlayer/Formation were considered and dropped — a measured diff proves the
   hardest-to-fake claim (material-shaded held-out geometry contributing correct pixels) far more
   rigorously than a screenshot.
2. **What must be correct:** per-channel R:G:B ratio matches the PCSX oracle (no overbright/ratio
   shift); the migrated `compositor_layer` fold reproduces the stock contribution. Occlusion (C) and
   caller-order (D) are already proven at the engine level by RD readback (tickets 09 + 10), so the
   game proof targets the end-to-end **color** path only.
3. **Capture:** windowed (RenderingDevice required, NOT `--headless`), `with`/`without`/`diff` PNGs +
   energy table stored under `research/working_documents/demi2_fold_audit/` tagged `new-engine`; on a
   faithful result, restore the "working fork proves the capability" line to
   `docs/sounding-7916-comment-draft.md` and link the artifact. User posts the sounding themselves.

**Preconditions (gates the capture):** engine built on `feature/render-to-compositor` + game migrated
onto `compositor_layer` (tickets 14 + 15). The additive seq-2 target only needs CLEAR seed; the
`TEXTURE` seed (ticket 13) is required only if the proof set is extended to sub/mix particles.

**Handoff:** `.scratch/compositor-spike/plans/04-demi2-proof-handoff.md`.
