# "Prims emit raw display texels" is true of the particle fold, but a TARGET for the real unit material

Grounded reading of the shipped shaders (2026-07-22) sharpened a claim the plan states loosely.

**What's precise now.** "Raw texel" = a sample taken with **no `source_color`**, so no sRGB→linear on
input. True for the unit atlas (`unit_sprite_body.gdshaderinc:640`) — but there the reason is that the
atlas stores **palette indices** (`:657`), so raw + `filter_nearest` are required to keep the index
intact, independent of the fold.

**The nuance the plan (§2) glosses.** Whether a prim's *contribution* is in display space depends on
the fragment's **output**, not just the input sample:
- The compositor's particle/mode fold shaders sample raw and **never re-encode** → output DISPLAY.
  The gamma fold works today. (`combat_displayspace_composite.glsl:258–262`: "linearizing the texel
  IS the defect this compositor removes.")
- The **real unit material** ends with `ALBEDO = pow(ALBEDO, 2.2)` (`:923`) → output **LINEAR**.
  Correct for its normal opaque Pass-1 use, but it is exactly the "defect" the fold must avoid.

So the plan's "the prims emit raw display texels" is **true of the current hand-ported particle fold**
and a **not-yet-guaranteed target** for the engine drawing the real unit material — which is precisely
**open question #1** ("Raw-value preservation — PROTOTYPE FIRST") in `engine-shaded-display-fold.md` §7.

**Implication for teaching.** Lesson 5 (the linear→display→linear→screen walk) must NOT assert the
fold "just works" for engine-drawn units. Frame Pass B's display-space contribution as guaranteed for
the particle path and *conditional on suppressing line 923* for the unit path. Aaron can now derive
this from the two files himself — mission-aligned (reason about the plan, don't take its word).
