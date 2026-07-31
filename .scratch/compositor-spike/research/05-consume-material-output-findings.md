# Findings — `spike/compositor-consume-material-output` vs. the settled compositor-render-layer proposal

**One-line summary:** The spike is a working, ~150-line Forward+-only `compositor_fold` implementation whose membership is **MATERIAL-SIDE** (a `render_mode`), which is the *opposite* axis from the settled proposal's per-instance property — so its caller-order sort and its held-out-target plumbing are directly reusable, but the opt-in mechanism, the stringly-typed target, and the use-case naming must all be reshaped; recommendation below is **reimplement clean on `feature/render-to-compositor`, porting the two load-bearing, unit-tested pieces (the sort seam + the scratch/seed/composite frame model) rather than the material-`render_mode` opt-in.**

Base for all diffs: `aac1c92f5f`. Subject repo: `/home/curry/Repos/godot`.

---

## 1. `compositor_fold` render_mode & membership (the load-bearing #7916 question)

**Parsed / registered in three places:**

- `servers/rendering/shader_types.cpp:245` — registers `compositor_fold` as a spatial `render_mode` flag (one push_back next to `unshaded`). This makes it parse on *every* renderer.
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp:122` — binds `actions.render_mode_flags["compositor_fold"] = &compositor_fold;`; field declared at `scene_shader_forward_clustered.h:256` (`bool compositor_fold = false;`), reset at `.cpp:56`.
- `servers/rendering/renderer_rd/forward_mobile/scene_shader_forward_mobile.cpp:123` — binds the same flag **only to detect+WARN** (`.cpp:193-200`, `WARN_PRINT_ONCE(... only supported on the Forward+ renderer ...)`); field at `scene_shader_forward_mobile.h:262`. Mobile has no fold pass, so a flagged material silently falls into the normal transparent pass — hence the loud warn.

**How a material/instance opts in — MATERIAL-SIDE:** membership is a property of the *shader*, not the instance. In `_fill_render_list` (`render_forward_clustered.cpp:1157-1163`), a transparent surface is routed by testing `surf->shader->compositor_fold`: if set it goes to `RENDER_LIST_COMPOSITOR_FOLD`, else `RENDER_LIST_ALPHA`. There is **no per-instance flag, no `VisualInstance3D` property, no scene-side identity resource.** Every instance of a `compositor_fold` material is held out; a `StandardMaterial3D` can never participate (no shader authoring hook). This is the single largest divergence from the settled proposal, whose §2 opt-in is a per-instance `render_layer : CompositorRenderLayer` property specifically so any `StandardMaterial3D` participates unchanged and so membership is a cull/fill-time routing tag rather than a shader-permutation axis.

> Note re. the #7916 GPU-driven objection: the proposal's whole membership argument is that a *per-instance* routing tag is more GPU-driven-native than a `render_mode`. The spike does exactly the thing the proposal warns against — membership *is* a shader-permutation axis here. So on the one #7916-critical question, the spike is on the wrong side and cannot be lifted as-is.

New render list enum slot: `RENDER_LIST_COMPOSITOR_FOLD` at `render_forward_clustered.h:84` (sibling of `RENDER_LIST_ALPHA`), with a matching per-list `MultiUmaBuffer` at `.h:411`.

---

## 2. `fold_order_sort.h` caller-order sort

**File:** `servers/rendering/renderer_rd/forward_clustered/fold_order_sort.h` (new, 83 lines).

- **Ordering key:** each surface's `owner->sorting_offset` (a `float`). `FoldOrderComparator` is a stable strict-weak-ordering over an index permutation: ascending `sorting_offset`, ties broken by submission index (`p_a < p_b`). NaN is canonicalized to `+INF` (`key()`, `fold_order_sort.h:52-54`) so a game-controlled uncapped float can't violate `SortArray`'s ordering contract. `compute_fold_order()` fills an identity permutation for <2 elements.
- **Where applied in the render loop:** `render_forward_clustered.h:738-762` defines `RenderList::sort_by_fold_order()`, which pulls `sorting_offset` per element, calls `compute_fold_order`, and reorders `elements`. It is invoked at `render_forward_clustered.cpp:1931` (`render_list[RENDER_LIST_COMPOSITOR_FOLD].sort_by_fold_order();`) — right where `RENDER_LIST_ALPHA` is sorted by `sort_by_reverse_depth_and_priority()`, but the fold list **never** sorts by depth.
- **Relation to the game's ordering:** the game's `OTDepthPrimOrder.order()` (LSD radix: depth-bucket → age → submit) runs entirely CPU-side in userland and produces one linear sequence; each run-instance's `sorting_offset` is stamped with that run's index (this is what `DepthMode.sorting_offset_for()` computes). The engine then folds *purely in that caller order*. Note `sorting_offset` normally biases depth-sorting in `_fill_render_list` (`.cpp:958-960` comment); for the fold list that perturbation of `inst->depth` is inert because the list is never depth-sorted. This uncapped-`float` key **supersedes the design doc's earlier `render_priority` (±127) plan** (design §6.2) — the shipped code uses `sorting_offset`, giving unbounded run count.
- **Unit-tested:** `tests/servers/rendering/test_fold_order_sort.cpp` (103 lines) exercises the pure comparator without a renderer — matching the proposal's Tests section ("pure ordering logic extracted and unit-tested under `tests/servers/rendering/`"). This is directly portable.

The proposal's §2 differs only in that the key lives on the *instance* (`render_layer_order : int`, exact int, no float32-ULP cliff) rather than reusing `sorting_offset`; the comparator shape (ascending key, stable insertion tie-break, extracted+unit-tested) is otherwise identical and reusable.

---

## 3. Held-out target / scratch plumbing (seed → fold → composite)

The spike's model is **compositor-owned, engine-fills-only** — the engine neither allocates nor clears the scratch.

- **Scope constant:** `render_scene_buffers_rd.h:51` adds `#define RB_SCOPE_COMPOSITOR_FOLD SNAME("compositor_fold")`. The header comment explicitly frames it as a *cross-repo stringly-typed contract*: the userland `CompositorEffect` (`FoldSurface.gd` Pass A/C) must `create_texture()/get_texture()` with the byte-identical scope+`RB_TEX_COLOR` subname or the handshake silently no-ops.
- **Allocation/seed:** NOT done by the engine. Pass A (a `PRE_TRANSPARENT` `CompositorEffect` in userland) creates the named texture under `("compositor_fold","color")` and seeds it (scene color → display space, coverage α=0). The engine checks `rb->has_texture(RB_SCOPE_COMPOSITOR_FOLD, RB_TEX_COLOR)` and *skips with a warn* if absent (`render_forward_clustered.cpp:2515-2517`) — it will not allocate an unseeded buffer.
- **Fold (engine Pass B):** `render_forward_clustered.cpp:2476-2555`, drawn **after the transparent resolve, before the POST_TRANSPARENT callback**. It fetches the scratch RID (`.cpp:2520`), validates format (`A2B10G10R10_UNORM_PACK32`, warn-and-continue on mismatch, `.cpp:2528-2531`), validates size + layer count (warn-and-**skip** on mismatch since framebuffer_create would hard-fail, `.cpp:2536-2545`), builds a multiview framebuffer against the scratch + shared scene `depth_texture`, and draws the fold list with `RD::DRAW_DEFAULT_ALL` (**LOAD** — preserves the seeded color and shared depth, no clear). The rp-uniform-set is set up earlier while buffers are valid (`.cpp:2434-2439`).
- **Composite (Pass C):** back in userland — the `POST_TRANSPARENT` `CompositorEffect` reads the scratch and resolves it (display→linear, RGB555 quantize, discards coverage≈0) into the scene.
- **Cleanup caveat:** `render_scene_buffers_rd.cpp:133-140` — `cleanup()` frees ALL named textures with no scope filter (including the compositor-owned scoped scratch), so the userland effect must recreate the scratch every frame and never cache the RID across a resize. Only comment lines were added here (6-line diff), no logic change.

Vs. the proposal §1: the proposal moves allocation *into the engine* (keyed by a `CompositorRenderLayer` resource identity via the `NTKey` store, `render_scene_buffers_rd.cpp:328-352`), replaces the magic string with a typed resource + a `get_layer_texture(resource)` accessor, and generalizes the seed to an enumerated `seed_source` (CLEAR | SCENE_COLOR | bound Texture) rather than a hardcoded userland-authored seed. The spike's frame *shape* (seed → held-out pass on resolved depth, depth-test/no-depth-write, LOAD not clear, composite after) matches the proposal exactly; the *ownership and naming* differ.

---

## 4. New public RenderingServer API surface

The upstream-facing surface this branch exposes is **deliberately minimal — one method, no enums, no new Resource:**

- `servers/rendering/rendering_server.h:1055` / `.cpp:2095-2101` — `bool RenderingServer::is_compositor_fold_supported() const;` returns `get_current_rendering_method() == "forward_plus"`. Bound at `rendering_server.cpp:3533` (`ClassDB::bind_method(D_METHOD("is_compositor_fold_supported"), ...)`). The header comment notes its *presence* also serves as a feature-detect (`has_method("is_compositor_fold_supported")`) on stock Godot.

**No changes to `scene/resources/compositor.{cpp,h}` and no changes to `servers/rendering/renderer_compositor.cpp`** (confirmed empty diff). There is **no** `CompositorRenderLayer` Resource, no new enums, no `get_layer_texture` accessor, no per-instance `VisualInstance3D` property. The only other engine-visible new symbols are internal: the `RENDER_LIST_COMPOSITOR_FOLD` enum, the `compositor_fold` shader render_mode word, and the `RB_SCOPE_COMPOSITOR_FOLD` scope macro.

So the entire public API delta is one capability-gate method — versus the proposal's much larger public surface (a new `CompositorRenderLayer : Resource` with `Format`/`SeedSource` enums, a `render_layers` exported array on `CompositorEffect`, a `render_layer` + `render_layer_order` property pair on `GeometryInstance3D`, and a `get_layer_texture` bound method).

---

## 5. This branch's own design docs (and where they diverge from the settled proposal)

- **`docs/adr/0001-fork-not-extension-for-compositor-fold.md`** — Decision: ship `compositor_fold` as a **custom engine fork**, not a GDExtension and not (yet) upstream. Rationale: Pass B lives in the scene renderer and no extension seam reaches it (`CompositorEffect` gets only `RenderData`; `RendererSceneRender` is plain C++, not `GDCLASS`; renderer method names are a hardcoded whitelist). Cost is the rebase tax; upstream stays a long-game exit.
- **`docs/compositor-fold-design.md`** — The converged "engine draws flagged materials through their real `.gdshader` into a clamping A2B10G10R10_UNORM scratch, in caller order" design. Contains a self-correction (2026-07-22) grounding it against the shipped FFT compositor: confirms the 3-pass scene-seeded **display-space** fold (A copy-in / B fold / C out) and that ordering is CPU-side via `OTDepthPrimOrder`. §6 still describes `render_priority` for cross-run order, which the shipped code superseded with `sorting_offset`.
- **`docs/compositor-fold-upstream-evaluation.md`** — Pressure-tests the feature as an incoming core PR. **Verdict: "Bounce as a core PR; viable as a rendering-team proposal only after reframing from the use-case (`compositor_fold`) to the general primitive."** Names the exact 🔴 tells: use-case-named render_mode (loudest), stringly-typed texture handshake, silent alpha-blend override, one-off capability method, no docs, no in-tree tests. Concludes the work is *subtraction* (the general primitive is tighter than the specific one).

**Where this branch's design differs from the settled proposal** (`proposal-draft-compositor-render-layer.md`): the branch's own upstream-eval already *predicts* the proposal's direction but did not implement it. The proposal took the eval's "reframe to the general primitive" verdict and went further on the membership axis — moving opt-in **off the material `render_mode` and onto a per-instance property + typed `CompositorRenderLayer` resource**, precisely to satisfy #7916's GPU-driven-rendering constraint (which a `render_mode`, being a shader-permutation axis, does not). The branch's design is material-side by construction; the proposal is instance-side by construction. They agree on the frame model, the caller-order sort, and the held-out-target concept; they disagree on membership, target ownership/naming, and seed generality.

---

## 6. Delta to the destination

**(a) Already matches the settled upstream-PR shape:**
- Caller-order sort: an extracted, NaN-robust, stable, **unit-tested** integer/float-key comparator with an identity-permutation fast path (`fold_order_sort.h` + `test_fold_order_sort.cpp`) — the proposal's §2 + Tests, essentially portable.
- Held-out target frame model: seed → draw held-out list into a *separate* target on the resolved scene depth, **depth-test / depth-write OFF**, LOAD (preserve seed) not clear, drawn after the scene resolve and before the consuming effect — the proposal's frame model verbatim (`render_forward_clustered.cpp:2476-2555`; depth-write forced off at `scene_shader_forward_clustered.cpp:383-390`).
- New render list + fill-branch + per-list instance buffer in Forward+ (the proposal's "engine implementation sketch": `RENDER_LIST_COMPOSITOR_LAYER` sibling of `RENDER_LIST_ALPHA`, a fill-branch, a sort policy, one draw site).
- Forward+-first with an explicit Mobile gate (warn) and a capability method — matches the proposal's "Forward+ first, RD-only, capability-gated, hard-fail/warn on unsupported."
- The wall it dissolves and the argument for core-not-addon are the same.

**(b) Would need changing/cleaning to become the settled shape:**
- **Membership axis flip (the big one):** replace the material `render_mode compositor_fold` (shader-permutation, StandardMaterial3D can't participate) with a per-instance `render_layer : CompositorRenderLayer` property routed at cull/fill time. This is a from-scratch mechanism, not an edit — it touches `VisualInstance3D`/`GeometryInstance3D`, the instance data path, and the fill branch.
- **Target ownership + naming:** replace the `RB_SCOPE_COMPOSITOR_FOLD` magic-string, userland-allocated scratch with engine-owned allocation keyed by a typed `CompositorRenderLayer` resource identity (NTKey store), plus a `get_layer_texture(resource)` accessor. Introduce the `CompositorRenderLayer : Resource` + `Format`/`SeedSource` enums and the `render_layers` exported array on `CompositorEffect`.
- **Generalize the seed:** add enumerated `seed_source` (CLEAR | SCENE_COLOR | bound Texture) instead of the hardcoded userland-authored display-space seed.
- **De-use-case-name everything:** rename `compositor_fold` → a capability-named primitive (e.g. render-layer); the eval flags this as the loudest tell.
- **Drop the silent coverage-α override** (`scene_shader_forward_clustered.cpp:350-366` forces alpha ADD/ONE/ONE) — the proposal makes coverage an explicit engine-written side attachment / `CUSTOM_BUFFER` step (§3), not a silent rewrite of the material's declared blend. Similarly the display-space/A2B10G10R10 clamp and RGB555 quantize are FFT policy that must not leak into the engine.
- **Fail-safe not fail-corrupt:** the warn-and-*continue* guard-rails (tonemapper/size/MSAA/format) must become structural invariants or hard-fails per the proposal's non-goals.
- **Docs + broaden tests** (class-ref for the new property/resource; the eval marks missing docs a 🔴).

**Recommendation — reimplement clean on `feature/render-to-compositor`, porting two pieces:** The spike is genuinely valuable as a *proof* (it verifies the frame model, the caller-order sort, and the scratch handshake on real GPU hardware) and its `fold_order_sort.h` + test and its render-loop pass structure are near-drop-in. But its *load-bearing membership decision is the exact opposite* of the settled proposal's — material-side `render_mode` vs. per-instance property — and that axis is the whole point of the proposal's #7916 GPU-driven-compatibility argument. Cleaning in place would mean deleting the `render_mode` registration in three files, deleting the shader-field routing, deleting the coverage-α and clamp overrides, deleting the magic-string scope, and *then* building the per-instance property + typed resource + engine-owned allocation from nothing — i.e. removing most of what the spike added before adding the real thing. Given `feature/render-to-compositor` is already the intended clean upstream-shaped branch and its engine code is empty, a clean reimplementation there — **porting only (1) the extracted, unit-tested sort seam and (2) the seed→held-out-pass→composite frame structure, re-keyed to a per-instance property and an engine-owned typed target** — is less error-prone and produces a reviewable, single-purpose diff, versus a clean-in-place that would read as a large subtract-then-add churn on a branch already carrying FFT-specific policy (display-space clamp, RGB555, coverage) and use-case naming throughout its docs and identifiers. Keep the spike as the reference/oracle; build the PR clean.

---

## 7. Diff size

**Engine files touched: 12** (excluding docs and the test file). Including the in-tree unit test: **13 files, +389 / −3 lines** across engine+test. (The full branch diff is 34 files / +3604 because it also carries ADR + design docs + a `teach-color-spaces` learning workspace, none of which are engine code.)

Touched engine files:
- `servers/rendering/shader_types.cpp` (+1) — register the render_mode
- `servers/rendering/renderer_rd/forward_clustered/fold_order_sort.h` (+83, new) — caller-order comparator
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` (+102/−1) — routing, sort call, fold pass
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h` (+32/−1) — enum, buffer, `sort_by_fold_order`
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.cpp` (+28) — flag bind, coverage-α + depth-write-off overrides
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h` (+1) — `compositor_fold` field
- `servers/rendering/renderer_rd/forward_mobile/scene_shader_forward_mobile.cpp` (+14) — flag bind + WARN
- `servers/rendering/renderer_rd/forward_mobile/scene_shader_forward_mobile.h` (+1) — field
- `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.cpp` (+5/−1) — cleanup comment
- `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h` (+6) — `RB_SCOPE_COMPOSITOR_FOLD`
- `servers/rendering/rendering_server.cpp` (+8) — `is_compositor_fold_supported` + bind
- `servers/rendering/rendering_server.h` (+7) — declaration
- (test) `tests/servers/rendering/test_fold_order_sort.cpp` (+103, new)
