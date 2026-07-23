# Aaron reasoned out shade-then-blend ordering; "raw index" ≠ "display color" untangled

2026-07-22, mid-Lesson-4 exchange.

**What he demonstrated (evidence-grade).** Unprompted, Aaron reasoned that you cannot add palette
*indices* — "surely you add AFTER color is applied." Correct. He caught a genuine conflation in the
Lesson 4 draft, which had collapsed two distinct steps into the phrase "keep the texel raw (display),
add it." This is understanding, not exposure: he derived the shade-then-blend ordering from first
principles and spotted the gap.

**The distinction now established (don't re-blur it).** Two independent uses of "no `source_color`":
1. On the **index** texture (`type1_tex`) + `filter_nearest` → protects the lookup *key*; nothing to
   do with color space.
2. On the **palette** texture (`type1_palette`) → resolved color stays in DISPLAY encoding,
   un-linearized → *this* is what makes the later hardware ADD a gamma add.
Order (grounded, `unit_sprite_body.gdshaderinc:510–525`): raw index → palette lookup → color ops →
ALBEDO → hardware ADD (outside the shader, on the resolved color, never the index).

**Implication for teaching.** He owns "shade first, blend second" and that the blend operates on
resolved colors. Future lessons (5, 6) can lean on this: the fold's "gamma add" is about the
*resolved ALBEDO* being a gamma number, not about the texel being raw. Lesson 4 was patched to add a
new §3 ("Indices don't get added — colors do"). Ties to [[0002-raw-texel-display-output-nuance]].
