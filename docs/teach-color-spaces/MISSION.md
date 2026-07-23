# Mission: Color spaces, depth, and the pass-by-pass fold pipeline

## Why
Aaron designed an engine↔compositor architecture for a PSX-style FFT game (the
"display-space fold"). He can *build* it, but he wants to *reason about it himself* —
to defend the plan, catch his own hand-waving, and make the open engineering calls in
`../engine-shaded-display-fold.md` §7 without taking anyone's word for it. That requires
owning the real-time-rendering color-management fundamentals underneath the plan.

## Success looks like
- Explain, unprompted, why the fold must happen in gamma while lighting happens in linear.
- Read the 5-pass diagram in `../engine-shaded-display-fold.md` and narrate each color-space
  transition (linear→display, display→linear, linear→screen) and *who* does it and *why*.
- Correctly answer all 7 handoff questions (see `NOTES.md`) from first principles, not analogy.
- Catch loose terminology — e.g. know that "display" and "screen" name one gamma space at two stages.

## Constraints
- Aaron is a strong systems/gameplay engineer, newer to color-management internals.
- He wants precise, grounded, first-principles explanations — no analogies that skip mechanics.
- "I'm confused" means the explanation skipped a step: go **finer**, not broader.
- Teach keyed to the actual pipeline passes (opaque → seed A → fold B → out C → tonemap).

## Out of scope (for now)
- Advancing the open engineering questions in `../engine-shaded-display-fold.md` §7. Teach first;
  build later, only when Aaron asks.
- Wide color gamuts (Rec.2020, P3), HDR display output, OKLab/perceptual color science beyond
  what the sRGB transfer function requires.
