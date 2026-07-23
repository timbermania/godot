# Course through-line complete: all 7 handoff questions answered (Lessons 1–6)

2026-07-23. Lesson 6 (tonemap, Q7) delivered; the six-lesson arc now covers every handoff question.

**What Aaron demonstrated across the arc (evidence-grade, not just coverage).** He didn't passively
receive these — he drove several corrections and derived results himself:
- Reasoned out shade-then-blend ordering and caught a real conflation in L4 (LR-0003).
- Derived the destination-encoding rule's consequence (normal transparents also linearize) with me.
- Interrogated the coverage/discard design, proposed a differential-compositing alternative, and — once
  shown the lossy-seed baseline problem — understood why coverage-discard wins here.
- Correctly predicted the assets are "pre-crushed" to 5-bit and that mix knocks values off the grid.
- Correctly reasoned the seed needs an explicit linear→gamma encode (scene is linear by default).

**Where he is now.** He can read the 5-pass diagram and narrate every color-space transition, who does
it, and why — from first principles, grounded in the real files. Mission success criteria substantially
met. Open-Q1 (raw-value preservation) he can already argue from L4.

**Implication for future sessions.** The *fundamentals* phase is done. Natural next phases, only if he
asks (mission says teach-first, don't advance the build): (a) the ordering lesson — OTDepthPrimOrder,
render_priority, runs, the DEMI/age tie-break (foreshadowed in L2/L3 but never given its own lesson);
(b) walking the remaining §7 open questions as reasoning exercises; (c) wisdom — posting his reasoning
to r/GraphicsProgramming. Do NOT start building unless he explicitly pivots the mission.
