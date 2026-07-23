# Starting floor: strong systems engineer, newer to color-management internals

Established from the session handoff (2026-07-22), not yet from demonstrated performance.

**Prior knowledge (disclosed):** Aaron is a strong systems/gameplay engineer and *designed* the
engine↔compositor display-space fold architecture himself. He is fluent in the *systems* shape of the
plan (passes, ownership, blend routing, ordering) but newer to the *color-management internals* that
justify it. So: teach the color-space "why," not the architecture "what" — he built the what.

**Target misconception to dissolve (his stated confusion):** he is unsure how "display space" and
"screen space" differ. They do not differ as color spaces — one gamma space, two stages. Lesson 1
targets exactly this. Record a follow-up LR once he demonstrates he can state it back correctly.

**Implication for ZPD:** skip introductory "what is a shader / framebuffer" material. Start at
linear-vs-gamma (the load-bearing foundation) and climb the 5-pass diagram from there.
