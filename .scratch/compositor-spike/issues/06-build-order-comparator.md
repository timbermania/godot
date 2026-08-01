# [Build · Step 1] Pure caller-order comparator, test-first

Type: task
Status: resolved
Blocked by: —

## Question

Build the pure caller-order comparator for the compositor render layer, test-first, with no renderer
dependency. Detail: `.scratch/compositor-spike/plans/02-engine-implementation-plan.md` § Step 1.

- Port SPIKE `fold_order_sort.h` → target
  `servers/rendering/renderer_rd/forward_clustered/compositor_layer_order_sort.h`, **re-keyed
  `float`→`int32_t`** (drop NaN canonicalization; keep the identity-permutation fast path for <2 elements
  and the `compute_order(uint32_t*, const int32_t*, uint32_t)` shape).
- Port SPIKE `test_fold_order_sort.cpp` → `tests/servers/rendering/test_compositor_layer_order_sort.cpp`;
  register in `tests/test_main.cpp`.

**Done:** `scons tests=yes` builds; `--test --test-case="*CompositorLayerOrder*"` passes (ascending order,
stable ties, identity for <2, negative/duplicate keys).

## Answer

**Done (2026-07-31).** Built on `feature/render-to-compositor`.

**Files created:**
- `servers/rendering/renderer_rd/forward_clustered/compositor_layer_order_sort.h` — port of SPIKE
  `fold_order_sort.h`, re-keyed `float`→`int32_t`. Seam is `struct CompositorLayerOrderComparator {
  const int32_t *orders; }` + `inline void compute_order(uint32_t *r_order, const int32_t *p_orders,
  uint32_t p_size)` (the ticket's required shape, verbatim). Ascending `render_layer_order`, ties
  broken by stable submission index (`p_a < p_b`); identity fast path for `<2` elements kept.
- `tests/servers/rendering/test_compositor_layer_order_sort.cpp` — port of SPIKE
  `test_fold_order_sort.cpp`. 5 cases / 18 assertions: ascending sort, stable-on-equal, ties within
  mixed keys, **negative + duplicate** keys (stable), identity for `<2`.

**Deltas from the spike (all per plan § Step 1):**
- **NaN canonicalization dropped.** The spike's `key()` helper (`Math::is_nan → +INF`) and its
  `[FoldOrderSort] NaN … folds last` regression case exist *only* because `sorting_offset` was an
  unvalidated float; an `int32_t` cannot be NaN, so the strict-weak-ordering hazard is gone by
  construction. `core/math/math_funcs.h` include dropped with it (only `sort_array.h` remains).
- Fractional test fixtures re-keyed to ints; test tag renamed `[FoldOrderSort]`→
  `[CompositorLayerOrderSort]` (so `--test-case="*CompositorLayerOrder*"` matches).

**Registration note (ticket said "register in `tests/test_main.cpp`" — NOT needed).** Godot's
`tests/SCsub` globs every `tests/**/*.cpp` and auto-generates `tests/force_link.gen.h` from each
`TEST_FORCE_LINK(name)` macro (consumed by `test_main.cpp:48` / `:81`). The `TEST_FORCE_LINK(
test_compositor_layer_order_sort)` in the new `.cpp` *is* the registration — which is why the spike
branch's `test_main.cpp` is byte-identical to base. No manual edit to `test_main.cpp` was made or
needed. Apply this to Steps that add further tests.

**Verification:**
```
scons platform=linuxbsd target=editor dev_build=yes tests=yes -j24   # exit 0, 00:02:27
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-case="*CompositorLayerOrder*"
  → test cases: 5 | 5 passed | 0 failed;  assertions: 18 | 18 passed | 0 failed;  Status: SUCCESS!
```
(The pure comparator has no RenderingDevice dependency, so `--headless` is fine here — unlike the
in-game fold run, which must be windowed per [[spike-fold-run-invocation]].)

**Not committed** — left as a working-tree change for the user to review/commit.
