/**************************************************************************/
/*  compositor_layer_order_sort.h                                         */
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

#include "core/templates/sort_array.h"

// Ordering key for the compositor render-layer list: each member instance stamps an exact
// `render_layer_order` (int32), so a layer draws its members in the CALLER's declared order
// without ever sorting by camera depth. The comparator is a STABLE strict-weak-ordering over an
// index permutation: ascending `render_layer_order`, and on ties (equal order key) it falls back
// to the submission index so equal-key instances keep the order they were added in — not the
// arbitrary order an unstable introsort gives on raw pointers.
//
// Kept as a standalone header (not inlined into RenderForwardClustered::RenderList) so the ordering
// can be unit-tested without constructing renderer instances (see
// tests/servers/rendering/test_compositor_layer_order_sort.cpp).
//
// The key is an exact `int32_t` (the general primitive's `render_layer_order`), not the spike's
// uncapped `float sorting_offset`: an int has no float32-ULP cliff and cannot be NaN, so the NaN
// canonicalization the float version needed to stay a valid strict weak ordering is gone.
struct CompositorLayerOrderComparator {
	const int32_t *orders = nullptr;

	_FORCE_INLINE_ bool operator()(uint32_t p_a, uint32_t p_b) const {
		const int32_t ka = orders[p_a];
		const int32_t kb = orders[p_b];
		if (ka < kb) {
			return true;
		}
		if (kb < ka) {
			return false;
		}
		return p_a < p_b; // Stable tie-break: preserve submission order on equal keys.
	}
};

// Fills `r_order[0 .. p_size)` with a stable permutation of the element indices ordered by
// (render_layer_order, submission index). `p_orders` holds one `render_layer_order` per element.
// A list of fewer than two elements is left as the identity permutation.
inline void compute_order(uint32_t *r_order, const int32_t *p_orders, uint32_t p_size) {
	for (uint32_t i = 0; i < p_size; i++) {
		r_order[i] = i;
	}
	if (p_size < 2) {
		return;
	}
	SortArray<uint32_t, CompositorLayerOrderComparator> sorter;
	sorter.compare.orders = p_orders;
	sorter.sort(r_order, p_size);
}
