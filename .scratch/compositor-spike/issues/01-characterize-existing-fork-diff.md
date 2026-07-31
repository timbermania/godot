# Characterize the existing compositor fork's engine diff

Type: research
Status: resolved
Blocked by: —

## Question

The FFT game currently renders its display-space fold against an existing custom engine fork:
`/home/curry/Repos/godot` @ `spike/forward-plus-display-space-additive`. This is the reference
implementation the new `feature/render-to-compositor` branch must clean up into the upstream-PR shape.

Produce a concrete inventory of that fork's engine-source changes vs stock Godot 4.8-dev:

1. Confirm this branch is the fork the game actually targets (the game's `*_fold` shaders use
   `render_mode ... compositor_fold`; the fork commits mention `display_additive` — reconcile the
   naming: is `compositor_fold` an alias, a game-side rename, or a second render_mode?).
2. `RENDER_LIST_DISPLAY_ADDITIVE` — what is this new render list, where is it declared, how does an
   instance/material get routed into it, and how/when is it drawn relative to opaque + transparent?
   (This is the "held-out render layer" seed.)
3. The material-facing `display_additive` render_mode — where is it parsed and what pipeline state /
   blend / attachment does it select (the UNORM/display-space clamp)?
4. The post-tonemap / display pass that composites the held-out list back in-frame — file, callback
   point, and how it maps onto the game's Pass A/B/C mental model (`FoldSurface` seed/resolve).
5. Any caller-order / sorting mechanism the fork carries (vs. the game doing it in
   `DepthMode.sorting_offset_for()` / `OTDepthPrimOrder`).

Give file:line references and, where useful, the diff hunks vs stock 4.8-dev. This inventory seeds
the engine implementation plan (ticket 02).

## Answer

Full inventory in `research/01-existing-fork-findings.md`. Headline:

**There are TWO sibling spike branches in `~/Repos/godot`, both off base `aac1c92f5f`:**
- `spike/forward-plus-display-space-additive` (the one this ticket pointed at) — the **ancestor**.
  Parses only `display_additive` (`shader_types.cpp:245`); diverts flagged transparent surfaces into a
  new `RENDER_LIST_DISPLAY_ADDITIVE` (`render_forward_clustered.cpp:1152`), cleared per-frame (`:949`),
  re-drawn **after** tonemap (`:2568`) into an `R8G8B8A8_UNORM` display target (hardware additive clamps
  at 1.0; `linear_to_srgb()` via a specialization bit). Single post-tonemap composite = the game's
  "Pass B resolve"; **no** PRE_TRANSPARENT seed pass; **no** caller-order mechanism (stock reverse-depth
  sort). ~7 load-bearing engine files. Carries dead Spike-2 code (`effects/display_space_additive.*`,
  never invoked).
- **`spike/compositor-consume-material-output` — the ACTUAL reference the game targets.** The game's
  `*_fold` shaders use `render_mode compositor_fold`, which exists only here. This branch renames the
  primitive to `compositor_fold` across forward_clustered **and** forward_mobile, `render_scene_buffers`,
  and `rendering_server`; adds a **caller-order sort** (`fold_order_sort.h`) and **scratch-buffer
  plumbing** (the held-out seed/resolve target); and ships its own `docs/adr/0001-fork-not-extension-
  for-compositor-fold.md` + `compositor-fold-design.md` + `compositor-fold-upstream-evaluation.md`.

**Consequence:** the engine work is less "build from scratch" and more "adopt/clean the near-complete
`compositor-consume-material-output` branch into the settled material-side #7916-pass shape." The deep
diff of *that* branch (not this ancestor) is what the impl plan needs → new ticket 05.

**Game-facing name to expose: `compositor_fold`** (not `display_additive`).
