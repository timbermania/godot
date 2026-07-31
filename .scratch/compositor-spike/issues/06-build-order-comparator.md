# [Build · Step 1] Pure caller-order comparator, test-first

Type: task
Status: open
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

<!-- filled on resolution -->
