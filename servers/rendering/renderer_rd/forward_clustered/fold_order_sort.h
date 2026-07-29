/**************************************************************************/
/*  fold_order_sort.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/math/math_funcs.h"
#include "core/templates/sort_array.h"

// Ordering key for the compositor-fold render list: the game stamps each run-instance's
// `sorting_offset` = its OTDepthPrimOrder run index, so the fold folds runs in the CALLER's
// order without ever sorting by camera depth. The comparator is a STABLE strict-weak-ordering
// over an index permutation: ascending sorting_offset, and on ties (equal offset) it falls back
// to the submission index so equal-offset prims keep the order they were added in — not the
// arbitrary order an unstable introsort gives on raw pointers.
//
// Extracted from RenderForwardClustered::RenderList into this header so the ordering can be
// unit-tested without constructing renderer instances (see tests/servers/rendering/test_fold_order_sort.cpp).
struct FoldOrderComparator {
	const float *offsets = nullptr;

	// Canonicalizes NaN to +INF so the comparator stays a valid strict weak ordering even on
	// garbage input. `sorting_offset` is a game-controlled, uncapped, unvalidated float; a raw
	// NaN key compares unordered against every element (neither `<` holds either way), which
	// violates SortArray's ordering contract (an assertion in dev builds, undefined order
	// otherwise). Folding a NaN-keyed prim last, deterministically, is the safe degradation.
	_FORCE_INLINE_ static float key(float p_offset) {
		return Math::is_nan(p_offset) ? (float)Math::INF : p_offset;
	}

	_FORCE_INLINE_ bool operator()(uint32_t p_a, uint32_t p_b) const {
		const float ka = key(offsets[p_a]);
		const float kb = key(offsets[p_b]);
		if (ka < kb) {
			return true;
		}
		if (kb < ka) {
			return false;
		}
		return p_a < p_b; // Stable tie-break: preserve submission order on equal offsets.
	}
};

// Fills `r_order[0 .. p_size)` with a stable permutation of the element indices ordered by
// (sorting_offset, submission index). `p_offsets` holds one sorting_offset per element.
// A list of fewer than two elements is left as the identity permutation.
inline void compute_fold_order(uint32_t *r_order, const float *p_offsets, uint32_t p_size) {
	for (uint32_t i = 0; i < p_size; i++) {
		r_order[i] = i;
	}
	if (p_size < 2) {
		return;
	}
	SortArray<uint32_t, FoldOrderComparator> sorter;
	sorter.compare.offsets = p_offsets;
	sorter.sort(r_order, p_size);
}
