/**************************************************************************/
/*  test_fold_order_sort.cpp                                              */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_fold_order_sort)

#include "core/templates/vector.h"
#include "servers/rendering/renderer_rd/forward_clustered/fold_order_sort.h"

#include <cmath>

namespace TestFoldOrderSort {

static Vector<uint32_t> order_of(const Vector<float> &p_offsets) {
	Vector<uint32_t> order;
	order.resize(p_offsets.size());
	compute_fold_order(order.ptrw(), p_offsets.ptr(), p_offsets.size());
	return order;
}

TEST_CASE("[FoldOrderSort] Sorts ascending by sorting_offset") {
	// Element indices carry offsets 3, 1, 2 -> folded order is the 1, 2, 3 elements.
	const Vector<uint32_t> order = order_of({ 3.0f, 1.0f, 2.0f });
	CHECK(order[0] == 1);
	CHECK(order[1] == 2);
	CHECK(order[2] == 0);
}

TEST_CASE("[FoldOrderSort] Stable on equal offsets: submission order preserved") {
	// All-equal offsets must yield the identity permutation, not an introsort shuffle.
	const Vector<uint32_t> order = order_of({ 5.0f, 5.0f, 5.0f, 5.0f });
	CHECK(order[0] == 0);
	CHECK(order[1] == 1);
	CHECK(order[2] == 2);
	CHECK(order[3] == 3);
}

TEST_CASE("[FoldOrderSort] Ties within mixed offsets keep insertion order") {
	// offsets: 0->2, 1->1, 2->2, 3->1  =>  1,3 (offset 1) then 0,2 (offset 2), each tie stable.
	const Vector<uint32_t> order = order_of({ 2.0f, 1.0f, 2.0f, 1.0f });
	CHECK(order[0] == 1);
	CHECK(order[1] == 3);
	CHECK(order[2] == 0);
	CHECK(order[3] == 2);
}

TEST_CASE("[FoldOrderSort] Negative and fractional offsets order correctly") {
	// -1.5 < 0.0 < 2.25 -> element 1, element 2, element 0.
	const Vector<uint32_t> order = order_of({ 2.25f, -1.5f, 0.0f });
	CHECK(order[0] == 1);
	CHECK(order[1] == 2);
	CHECK(order[2] == 0);
}

TEST_CASE("[FoldOrderSort] Fewer than two elements is the identity permutation") {
	const Vector<uint32_t> one = order_of({ 42.0f });
	REQUIRE(one.size() == 1);
	CHECK(one[0] == 0);

	const Vector<uint32_t> none = order_of(Vector<float>());
	CHECK(none.size() == 0);
}

TEST_CASE("[FoldOrderSort] NaN offset preserves a valid ordering and folds last") {
	// Regression guard for the strict-weak-ordering hazard: a NaN sorting_offset must not
	// trip SortArray's dev-build ordering assert nor produce a non-deterministic result.
	// NaN canonicalizes to +INF, so the finite elements keep their order and the NaN folds last.
	const float nan = NAN;
	const Vector<uint32_t> order = order_of({ 2.0f, nan, 1.0f });
	CHECK(order[0] == 2); // offset 1.0
	CHECK(order[1] == 0); // offset 2.0
	CHECK(order[2] == 1); // NaN, canonicalized to +INF, folds last
}

} // namespace TestFoldOrderSort
