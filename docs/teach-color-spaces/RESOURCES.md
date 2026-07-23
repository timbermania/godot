# Color Spaces & the Fold Pipeline — Resources

## Knowledge

- [GPU Gems 3, Ch. 24 — "The Importance of Being Linear" (NVIDIA)](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-24-importance-being-linear)
  The canonical, high-trust primary source on linear vs. gamma in real-time rendering. Use for:
  why lighting/blending must be linear, the mid-gray "0.5 isn't half the light" example, mipmap errors.
- [John Novak — "What Every Coder Should Know About Gamma" (2016)](https://blog.johnnovak.net/2016/09/21/what-every-coder-should-know-about-gamma/)
  Friendlier, deeply-worked walkthrough with images. Use for: intuition on the transfer curve,
  gamma-correct blending/antialiasing, why naive sRGB math looks wrong.
- [LearnOpenGL — Gamma Correction](https://learnopengl.com/Advanced-Lighting/Gamma-Correction)
  Practical, code-level. Use for: encode/decode in shaders, the ~2.2 exponent, framebuffer sRGB.
- [LearnOpenGL — Depth Testing](https://learnopengl.com/Advanced-OpenGL/Depth-testing)
  Canonical friendly source on the z-buffer. Use for: the depth test, near→far encoding, z-fighting (Q2).
- [Godot Docs — High Dynamic Range](https://docs.godotengine.org/en/stable/tutorials/3d/high_dynamic_range.html)
  Engine-specific. Use for: how Godot keeps the scene linear and where tonemap sits. Ties to Forward+.
- [Godot Docs — `Color` class](https://docs.godotengine.org/en/stable/classes/class_color.html)
  Reference for `srgb_to_linear()` / `linear_to_srgb()` and the sRGB-encoding-by-default convention.
- [GDQuest — Tonemap glossary entry](https://school.gdquest.com/glossary/tonemap)
  Short. Use for: Linear vs. Reinhard vs. Filmic vs. ACES operators (Q7 — why the compositor needs Linear).

### In-repo primary sources (the code the concepts are grounded in)
- `../engine-shaded-display-fold.md` — the plan + the 5-pass diagram (the spine of this course).
- `../compositor-fold-design.md`, `../compositor-consume-material-output-feasibility.md` — errata blocks up top.
- `../../../fft-monorepo-compositor/godot-learning/assets/shaders/combat_displayspace_composite.glsl`
  — the fold fragment; its comments state the raw-sRGB/display fact outright.
- `.../assets/shaders/unit.gdshader` + `unit_sprite_body.gdshaderinc` — the raw-texel PSX material.
- `.../src/effects/CombatDisplaySpaceComposite.gd` — the 3-pass fold (A/B/C in its header).
- `.../src/effects/OTDepthPrimOrder.gd` — the depth+age sort.

## Wisdom (Communities)
- [r/GraphicsProgramming](https://reddit.com/r/GraphicsProgramming) — high-signal, low bro-science.
  Use for: sanity-checking color-space reasoning and the "blend is color-space-agnostic" claim.
- [Godot Engine — rendering discussions (GitHub / forum)](https://github.com/godotengine/godot/discussions)
  Use for: Forward+ vs. mobile fragment-output behavior, compositor-effect callbacks.
- (Aaron: no community preference recorded yet — ask before proposing participation.)

## Gaps
- No single high-trust source ties *PSX-era gamma-framebuffer blending* to modern linear pipelines;
  that synthesis is the design docs' own contribution. Lessons must bridge it explicitly.
