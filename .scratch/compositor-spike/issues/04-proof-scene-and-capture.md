# Define the proof scene and capture method

Type: grilling
Status: open
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

<!-- filled on resolution -->
