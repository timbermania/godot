# Engine implementation plan — general `compositor_layer` render-layer primitive

**Resolves:** wayfinder ticket 02. **Branch to build on:** `feature/render-to-compositor`
(`/home/curry/Repos/godot-compositor-consume-material`, engine currently docs-only).
**Oracle to port from:** `/home/curry/Repos/godot @ spike/compositor-consume-material-output`
(the proven 12-file `compositor_fold` impl; diff base `aac1c92f5f`).
**Approach:** reimplement clean (ticket 05), porting only the two proven pieces — the pure caller-order
comparator and the seed→held-out-pass→composite frame shape.

All file:line anchors below are in the **target** tree unless prefixed `SPIKE:`.

---

## Two locked decisions (from the ticket-02 grilling, 2026-07-31)

1. **Membership = a general `render_mode compositor_layer`** — a shader-permutation, batch-safe by reduz's
   "sort by shader type" GPU-driven construction (`research-gpu-driven-and-unification.md` §1b). NOT the
   use-case name `compositor_fold`. A `StandardMaterial3D` cannot join without a `ShaderMaterial` — accepted.
2. **Target identity = the member material carries `render_layer : CompositorRenderLayer`** (a resource
   property). Engine owns allocation keyed by that resource's identity (NTKey store), N layers coexist, the
   effect reads via `get_layer_texture(resource)`. The magic-string scope `RB_SCOPE_COMPOSITOR_FOLD` is
   **dropped**. Preserves the proposal §1 public API.

---

## How the two decisions reshape the spike's mechanism

| Concern | Spike (`compositor_fold`) | This plan (`compositor_layer`) |
|---|---|---|
| Membership | `render_mode compositor_fold` shader flag `surf->shader->compositor_fold` (SPIKE fill `:1157-1160`) | `render_mode compositor_layer` shader flag `surf->shader->compositor_layer` — same seam, general name |
| Target | one global scratch, magic string `RB_SCOPE_COMPOSITOR_FOLD`, **userland-allocated** | N engine-allocated targets keyed by `CompositorRenderLayer` identity (NTKey) |
| Which target | implicit (only one) | material's `render_layer` resource → NTKey → the member's partition of the list |
| Order key | `owner->sorting_offset` (uncapped float, SPIKE `fold_order_sort.h`) | per-instance exact `int render_layer_order` (no float32-ULP cliff) |
| Seed | hardcoded userland display-space seed | `seed_source` enum on the layer resource (CLEAR + bound-Texture in v1) |
| Coverage-α / clamp / RGB555 | forced in engine (SPIKE `scene_shader:350-390`) | **dropped** — FFT policy, must not leak into engine |
| Guard-rails | warn-and-continue (SPIKE fold pass `:2505-2545`) | structural invariants / hard-fail naming the renderer |

**Multi-layer routing note (new seam the spike never needed).** `RenderListType` is a fixed enum
(`render_forward_clustered.h:78-83`, `RENDER_LIST_MAX`), so N dynamic layers cannot each be a static list
slot. Design: **one** `RENDER_LIST_COMPOSITOR_LAYER` collects every member surface at fill time; at pass
time it is **stably partitioned by layer identity** (NTKey), and each partition is drawn into its own
engine-allocated framebuffer, preserving caller order *within* each partition. For the single-fold proof
there is exactly one partition, so this is a no-op fast path. Documented corner case: two *different* layers
whose members share the *same* shader variant sub-split that variant's future GPU indirect draw list by
target — rare, irrelevant to the proof, noted in the PR.

---

## Ordered implementation steps

Each step is scoped to ~one agent session and graduates into a task ticket from the map's "Not yet specified".

### Step 1 — Pure caller-order comparator, test-first (no renderer)
- **Files:** `servers/rendering/renderer_rd/forward_clustered/compositor_layer_order_sort.h` (new, port of
  SPIKE `fold_order_sort.h`); `tests/servers/rendering/test_compositor_layer_order_sort.cpp` (new, port of
  SPIKE `test_fold_order_sort.cpp`); register in `tests/test_main.cpp`.
- **Seam:** `struct CompositorLayerOrderComparator { const int32_t *orders; }` — ascending `render_layer_order`,
  ties broken by submission index (`p_a < p_b`, stable). Re-keyed **`float`→`int32_t`**: drop the NaN
  canonicalization (an int can't be NaN); keep the identity-permutation fast path for `<2` elements and the
  `compute_order(uint32_t *r_order, const int32_t *p_orders, uint32_t p_size)` shape verbatim.
- **Risk:** none structural — pure, standalone, deterministic. This is the quality-bar anchor the proposal's
  Tests section promises.
- **Done:** `scons tests=yes` builds; `--test --test-case="*CompositorLayerOrder*"` passes (ascending order,
  stable ties, identity for <2, negative/duplicate keys).

### Step 2 — Register the `compositor_layer` render_mode + shader-variant flag (Forward+), Mobile gate
- **Files:** `servers/rendering/shader_types.cpp` (one `push_back({ PNAME("compositor_layer") })` next to
  `unshaded` at `:244`); `scene_shader_forward_clustered.h`/`.cpp` (bool field + `render_mode_flags["compositor_layer"]`
  bind, mirroring SPIKE `scene_shader_forward_clustered.cpp:122`, `.h:256`); `scene_shader_forward_mobile.*`
  (bind the flag **only** to `WARN_PRINT_ONCE` "Forward+ only", mirroring SPIKE mobile `:123,193-200`).
- **Seam:** the render_mode makes membership a distinct shader variant (the batch-safety property). Nothing
  routes yet — the flag just parses and is readable as `surf->shader->compositor_layer`.
- **Risk:** low. **Do NOT port** the spike's coverage-α override or forced depth-write-off *here* — those are
  Step 6 policy decisions, not membership.
- **Done:** a `ShaderMaterial` with `render_mode compositor_layer` compiles on Forward+; a warn fires on Mobile;
  no behavioral change yet.

### Step 3 — `CompositorRenderLayer : Resource` (the identity token) + engine-owned allocation
- **Files:** `scene/resources/compositor_render_layer.{h,cpp}` (new `Resource` subclass); register in
  `register_scene_types.cpp`; NTKey derivation + allocation in
  `servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.{h,cpp}` (allocate-on-first-access keyed by
  identity, sibling to the existing named-texture store `create_texture`/`get_texture`/`has_texture` at
  `render_scene_buffers_rd.cpp:52-56`, freed in `cleanup()` `:134-137`).
- **Seam:** two-field identity token per proposal §1 — `format : Format` (enumerated, #7916 set) and
  `seed_source : SeedSource` — plus `stage : EffectCallbackType`. NOT a policy bag; size/view_count/depth-source
  are structural invariants, not fields. The engine derives the internal key from the resource's identity
  (`get_instance_id()` / RID map) so two references to the same `.tres` resolve to one target and there is no
  first-writer-wins aliasing.
- **Risk:** medium — this is the main *new* public surface. Keep it minimal; every extra field is a review
  liability (the eval's 🔴 "policy bag" tell).
- **Done:** creating a `CompositorRenderLayer`, referencing it twice, resolves to one engine-allocated texture
  of the declared format at render-target size with correct view_count; class-ref doc stub added.

### Step 4 — Per-instance `render_layer` + `render_layer_order`, plumbed to the fill routing
- **Files:** `scene/3d/visual_instance_3d.{h,cpp}` (`GeometryInstance3D` at `:90` — add
  `render_layer : CompositorRenderLayer` + `render_layer_order : int`, bind + `ADD_PROPERTY` following the
  `extra_cull_margin` pattern `:185-186`); `servers/rendering/rendering_server.h` +
  `renderer_rd/forward_clustered/render_forward_clustered.*` (`geometry_instance_set_render_layer*` on
  `GeometryInstanceForwardClustered`, mirroring how per-instance attrs reach the renderer); new render list
  `RENDER_LIST_COMPOSITOR_LAYER` in the enum (`render_forward_clustered.h:78-83`, bump `RENDER_LIST_MAX`,
  add the matching `MultiUmaBuffer` at `:409` and `RenderList` slot `:739`).
- **Seam:** the fill-branch. In `_fill_render_list` at the alpha routing site (`render_forward_clustered.cpp:1145-1157`)
  add, before the ALPHA add: `if (surf->shader && surf->shader->compositor_layer && inst->render_layer.is_valid())
  render_list[RENDER_LIST_COMPOSITOR_LAYER].add_element(surf); else …` — routes members out of opaque/alpha.
  Note per research §1a this is "one more branch in a loop that already branches per surface" — free on the CPU
  path. `render_layer_order` rides the instance data (batch-neutral: it only reorders *within* the already-
  separated list; it is not a dispatch/grouping axis).
- **Risk:** medium-high — the instance-attribute plumbing (`VisualInstance3D` → RS → `GeometryInstanceForwardClustered`)
  is the widest touch. The `render_layer` resource identity must reach the surface for Step 5's partition.
- **Done:** a held-out mesh disappears from the normal scene (routed into the new list, not yet drawn); the
  list orders by `render_layer_order` via Step 1's comparator (add `sort_by_layer_order()` next to
  `sort_by_reverse_depth_and_priority()` `render_forward_clustered.h:728`, called where ALPHA is sorted `:1918`).

### Step 5 — The held-out pass: seed → draw into the engine target on resolved depth → hand off
- **Files:** `render_forward_clustered.cpp` at the `POST_TRANSPARENT` dispatch site (`:2459`; pass drawn just
  *before* it, matching SPIKE `:2476-2555`); `_setup_render_pass_uniform_set` for the new list; framebuffer via
  `FramebufferCacheRD::get_cache_multiview(view_count, layer_texture, depth_texture)`.
- **Seam (frame shape, ported verbatim in structure):** for each layer partition — (a) **seed** the engine
  target per `seed_source` (CLEAR or bound Texture in v1; see Step 7); (b) build a multiview framebuffer against
  the engine target + the **shared resolved scene `depth_texture`**; (c) draw the partition with
  `RD::DRAW_DEFAULT_ALL` (**LOAD** — preserve seed + depth, no clear), depth-**test** vs resolved scene depth,
  depth-**write OFF**. Uniform set built earlier while buffers valid (SPIKE `:2434-2439`).
- **Risk:** high — the load-bearing render-loop edit. Depth-write-off is the "never corrupts scene depth"
  invariant; enforce it structurally in the pipeline state for this list (Step 6), not via a material forgetting
  `depth_draw_never`.
- **Done:** the held-out mesh renders through its real material into the layer texture, occluded by opaque scene
  geometry, in caller order; a trivial `CompositorEffect` reading `get_layer_texture()` composites it back and
  the mesh reappears at the composited stage. This is first end-to-end pixels.

### Step 6 — Strip FFT policy; convert guard-rails to invariants/hard-fails
- **Files:** `scene_shader_forward_clustered.cpp` (the SPIKE overrides at `:350-390`); the Step-5 pass guards.
- **Seam:** **DROP** the silent coverage-α ADD/ONE/ONE override, the A2B10G10R10 display-space clamp, and the
  RGB555 quantize — all FFT policy. Keep **only** the structural depth-write-off for this list. Convert the
  spike's warn-and-**continue** format/size/MSAA/layer guards (SPIKE `:2505-2545`) into **structural invariants**
  (size = render-target size and view_count = scene view_count *by construction*, since the engine now allocates
  the target) or **hard-fails naming the renderer** for genuinely unsupported configs (Mobile, non-RD). Coverage,
  where a client needs it, becomes an explicit engine-written side attachment (Step 7), never a silent blend rewrite.
- **Risk:** medium — must confirm the fold still renders after removing the clamp/coverage (the FFT *game* re-adds
  its display-space policy in its own Pass A/C effect; the engine stays neutral). This is validated in ticket 03/04.
- **Done:** engine diff carries zero FFT-specific constants; unsupported configs hard-fail with a renderer-named
  error instead of warn-and-corrupt.

### Step 7 — `get_layer_texture` accessor, `render_layers` effect property, capability method, docs
- **Files:** `scene/resources/compositor.{h,cpp}` (`CompositorEffect` at `:39` — add
  `render_layers : Array[CompositorRenderLayer]` exported property following the `access_resolved_color` bind
  pattern `compositor.cpp:48-50`, and a `get_layer_texture(resource)` method callable from the render callback);
  `rendering_server.{h,cpp}` (a `bool is_compositor_layer_supported()` capability gate ≈ SPIKE, returning
  `get_current_rendering_method() == "forward_plus"`, bound near `get_current_rendering_method()`
  `rendering_server.h:1047`); `doc/classes/CompositorRenderLayer.xml`, `CompositorEffect.xml`,
  `GeometryInstance3D.xml`.
- **Seam:** the effect declares produced layers as a **property** (live-read each frame, verified proposal §1 /
  Attack #6 — no per-frame GDScript dispatch), reads by handing back the **same resource object** (no string, no
  index). `get_layer_texture` is the one new accessor.
- **Risk:** low-medium — mostly binding + docs. Missing docs is a 🔴 in the upstream-eval; do them here.
- **Done:** the minimal in-tree repro (one held-out mesh + one effect declaring the layer) works from GDScript
  with no C++ knowledge; `has_method("is_compositor_layer_supported")` feature-detects on stock Godot.

---

## Confirmable defaults adopted (veto on review)

1. **Order key = exact `int32_t render_layer_order`** (proposal §2; research endorses over the spike's uncapped
   float `sorting_offset` — no float32-ULP cliff, no NaN canonicalization needed). Ties break by stable
   submission/fill order.
2. **Seed sources shipped in v1 = `CLEAR` + bound `Texture` only. `SCENE_COLOR` is DEFERRED** to a named future
   extension. Rationale (proposal Attack #3): `SCENE_COLOR` is only coherent when the effect *replaces* the scene
   with the layer; composited over/additively it double-exposes, and disambiguating needs an engine-written
   coverage channel (the minimal `CUSTOM_BUFFER` step). Shipping `CLEAR` + bound-`Texture` keeps v1 honest without
   the coverage attachment. **The FFT fold uses a scene-color-like seed — but it authors that seed itself in its
   userland Pass A and composites as a replace, so a bound-`Texture` seed covers it; the engine need not offer
   `SCENE_COLOR` for the proof.** Confirm this doesn't block ticket 03.
3. **Multi-layer via one list + pass-time partition by NTKey**, not N enum slots (enum is fixed-size). Single-fold
   proof hits the one-partition fast path.
4. **General name `compositor_layer`** for the render_mode, `CompositorRenderLayer` for the resource,
   `render_layer`/`render_layer_order` for the instance props.

---

## Open questions (could not fully resolve from source; carry into the build)

- **Q1 — order-key transport.** Confirm the cleanest per-instance channel for `render_layer_order` into
  `GeometryInstanceForwardClustered` (the spike reused the existing `sorting_offset` float field; we want a
  dedicated `int32_t` so we don't overload depth-bias). Likely a new field + `geometry_instance_set_*` setter;
  verify against the instance-data upload path in `_fill_instance_data`.
- **Q2 — resource-identity → NTKey.** Confirm whether keying on `Resource::get_instance_id()` (stable per
  in-memory resource) is sufficient, or whether the target should key on the resource RID; check lifetime vs the
  per-frame allocate-on-access model (`render_scene_buffers_rd.cpp:328-352` region) so a resize/free doesn't
  dangle. The proposal assumes engine-owned allocation keyed by identity — verify the NTKey map accepts a
  non-`SNAME` key or introduce a small identity→scope adapter.
- **Q3 — partition storage.** Whether the pass-time partition is a `HashMap<NTKey, LocalVector<index>>` rebuilt
  per frame or a stable secondary sort within the single list (sort by `(layer_identity, render_layer_order)`,
  then walk contiguous runs). The latter reuses Step 1's comparator with a compound key and avoids per-frame
  allocation — preferred, confirm feasibility.
- **Q4 — `stage` field scope.** The proposal allows a layer to declare an earlier (pre-resolve/MSAA) stage.
  v1 targets the post-resolve `POST_TRANSPARENT` default only; confirm the earlier-stage path is deferred (it
  changes MSAA sample count and the frame-shape anchor) so it doesn't expand Step 5.
- **Q5 — Mobile.** Confirm hard-fail vs warn-and-skip on Mobile registration. Proposal non-goals say hard-fail
  naming the renderer; the spike warns. Pick hard-fail at registration for a member material on Mobile.
