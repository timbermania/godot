# Teaching Notes — how Aaron wants to be taught

## Learner
- Strong systems/gameplay engineer. Newer to real-time-rendering color-management internals.
- Wants **precise, grounded, first-principles** explanations. No analogies that gloss mechanics.
- Sharp at catching hand-waving. Respects "let me check the actual file" far more than a confident
  wrong answer. When unsure, read the real code *with* him rather than asserting.
- **"I'm confused" = the explanation skipped a step.** Go finer, not broader.

## Teaching approach
- Teach **keyed to the actual pipeline passes** — "step by step, pass by pass."
  The 5-pass diagram in `../engine-shaded-display-fold.md` §4 is the lesson-plan spine:
  (1) opaque · (2) seed A · (3) fold B · (4) out C · (5) tonemap.
- Short lessons. One tangible win each. Stay inside working memory.
- Ground every claim in a citation or the real code.

## The 7 handoff questions (cover every one, keyed to passes)
1. linear / gamma / display / screen space — and how display vs screen differ.   → Lesson 1 ✅
2. what is the depth buffer.                                                     → Lesson 2 ✅
3. how do blend prims get a depth buffer if not in the opaque scene.             → Lesson 2 ✅
4. how are we filtering what does / doesn't render in the opaque scene.          → Lesson 3 ✅
5. why linear → display → linear → screen.                                       → Lesson 5 ✅
6. what is a raw texel.                                                          → Lesson 4 ✅
7. what is a tone map.                                                           → Lesson 6 ✅ (ALL DONE)

## Accuracy cautions (prior agent got these loose — do NOT repeat)
- **"display" vs "screen" = ONE gamma space at two stages.** This is his actual confusion. Nail it. (L1)
- **Hardware blend is color-space-agnostic.** `BLEND_OP_ADD` adds raw numbers; a blend is "gamma-space"
  iff both operands hold gamma numbers — not because of a special blender. Load-bearing insight.
- **Mobile does NOT blend in display space "for free"** (corrected). Mobile outputs linear ÷ luminance_multiplier;
  tonemap is later. **Forward+ is the target.**
- Prior agent twice over-generalized from partial code reads. When unsure, read the file with Aaron.

## Session log
- 2026-07-22: Workspace created. Mission set from handoff. Resources grounded via web (GPU Gems 3 Ch24,
  Novak, LearnOpenGL, Godot docs). Delivered Lesson 1 (linear vs gamma; display=screen=one gamma space).
- 2026-07-22: Delivered Lesson 2 (depth buffer; test-vs-write two switches; Q2+Q3). Grounded live in
  real shaders — unit.gdshader (depth_draw_opaque), unit_additive.gdshader + tile_overlay_mode0.gdshader
  (depth_draw_never = test on/write off), screen_color_mode0 (depth_test_disabled = always on top).
  Key teaching move: test and write are INDEPENDENT switches; fold prims never write, so prim-vs-prim
  order falls to draw order (OTDepthPrimOrder) — foreshadows the ordering lesson.
- 2026-07-22: Delivered Lesson 3 (filtering/routing; Q4). Grounded in the REAL Spike-1 engine diff on
  this branch (Forward+/forward_clustered): uses_alpha_pass() @ scene_shader_forward_clustered.h:283
  (transparent if alpha OR non-mix blend OR depth_draw_never OR depth_test_disabled); the routing loop
  @ render_forward_clustered.cpp:1146-1160 (compositor_fold peels ALPHA→COMPOSITOR_FOLD); render_mode
  registered @ shader_types.cpp:245. Key teaching move: the filter's depth conditions ARE Lesson 2's
  two switches — same fork seen from the routing side. Verified engine change is real, not asserted.
- 2026-07-22: Delivered Lesson 4 (raw texel; Q6) + the load-bearing "blend has no color space" insight.
  IMPORTANT grounded correction (see LR-0002): the shipped unit material linearizes at line 923
  (pow 2.2 → LINEAR), so "prims emit raw display texels" is true of the particle fold but a TARGET for
  the engine-drawn unit material — exactly plan open-Q1. Taught it honestly as reading the two files.
  Lesson 5 must NOT assert the unit fold "just works" in display.
- 2026-07-22: Aaron caught a real conflation in Lesson 4 (raw INDEX vs. display COLOR) and reasoned out
  shade-then-blend ordering himself (LR-0003). Patched Lesson 4 with new §3 "Indices don't get added —
  colors do" (grounded @ unit_sprite_body.gdshaderinc:510-525) + fixed the punchline wording. He owns
  "shade first, blend second; the blend adds resolved colors, not indices." Lean on this in L5/L6.
- 2026-07-22: Promoted the earned vocabulary into GLOSSARY.md (linear/gamma space, transfer function,
  texel/raw texel, source_color, color-vs-data texture, depth buffer/test/write, routing, compositor_fold,
  color-space-agnostic blend, DESTINATION-ENCODING RULE). Then delivered Lesson 5 (Q5, the round trip),
  grounded in CombatDisplaySpaceComposite.gd:25-36 (A/B/C contract) + the Linear-tonemap requirement.
  Backbone = destination-encoding rule; explicitly resolved Aaron's earlier round-trip question (decode
  is faithful; color moves only in the Pass-B gamma add + Pass-C RGB555 quantize). Honesty box carries
  open-Q1 forward. Only Lesson 6 (tonemap, Q7) remains to finish the 7 handoff questions.
- 2026-07-23: Delivered Lesson 6 (tonemap, Q7) — FINALE. All 7 handoff questions now covered (LR-0004).
  Grounded in CombatDisplaySpaceComposite.gd:36 ("Run under Linear tonemap (ACES/AgX corrupt the add)")
  + GDQuest/Godot HDR docs. Key closure: Pass C decode + Linear tonemap encode = identity re-encode, so
  the folded display color survives to screen unchanged; the detour to linear exists only to share one
  output path with the HDR background. Between-lesson exchanges also resolved: compositor role (read-
  modify-write, not "owns the screen"), seed = base-coat/blend backdrop, coverage/discard protects the
  pristine HDR background from the lossy PSX round trip, 5-bit RGB555 crush (mix knocks off-grid), seed
  needs explicit linear→gamma encode.
  NEXT (only if he asks): ordering lesson (OTDepthPrimOrder/render_priority/runs/DEMI age tie-break) —
  foreshadowed in L2/L3, never its own lesson. Or §7 open questions as reasoning exercises. Teach-first.
