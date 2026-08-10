/**************************************************************************/
/*  test_compositor_layer_order_sort.cpp                                  */
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

TEST_FORCE_LINK(test_compositor_layer_order_sort)

#include "core/templates/vector.h"
#include "servers/rendering/renderer_rd/forward_clustered/compositor_layer_order_sort.h"

namespace TestCompositorLayerOrderSort {

static Vector<uint32_t> order_of(const Vector<int32_t> &p_orders) {
	Vector<uint32_t> order;
	order.resize(p_orders.size());
	compute_order(order.ptrw(), p_orders.ptr(), p_orders.size());
	return order;
}

TEST_CASE("[CompositorLayerOrderSort] Sorts ascending by render_layer_order") {
	// Element indices carry order keys 3, 1, 2 -> drawn order is the 1, 2, 3 elements.
	const Vector<uint32_t> order = order_of({ 3, 1, 2 });
	CHECK(order[0] == 1);
	CHECK(order[1] == 2);
	CHECK(order[2] == 0);
}

TEST_CASE("[CompositorLayerOrderSort] Stable on equal keys: submission order preserved") {
	// All-equal keys must yield the identity permutation, not an introsort shuffle.
	const Vector<uint32_t> order = order_of({ 5, 5, 5, 5 });
	CHECK(order[0] == 0);
	CHECK(order[1] == 1);
	CHECK(order[2] == 2);
	CHECK(order[3] == 3);
}

TEST_CASE("[CompositorLayerOrderSort] Ties within mixed keys keep insertion order") {
	// keys: 0->2, 1->1, 2->2, 3->1  =>  1,3 (key 1) then 0,2 (key 2), each tie stable.
	const Vector<uint32_t> order = order_of({ 2, 1, 2, 1 });
	CHECK(order[0] == 1);
	CHECK(order[1] == 3);
	CHECK(order[2] == 0);
	CHECK(order[3] == 2);
}

TEST_CASE("[CompositorLayerOrderSort] Negative and duplicate keys order correctly and stably") {
	// keys: 0->2, 1->-1, 2->0, 3->-1  =>  the two -1 keys (elements 1, 3) come first in submission
	// order, then 0 (element 2), then 2 (element 0). Exercises negative keys and a duplicate tie.
	const Vector<uint32_t> order = order_of({ 2, -1, 0, -1 });
	CHECK(order[0] == 1);
	CHECK(order[1] == 3);
	CHECK(order[2] == 2);
	CHECK(order[3] == 0);
}

TEST_CASE("[CompositorLayerOrderSort] Fewer than two elements is the identity permutation") {
	const Vector<uint32_t> one = order_of({ 42 });
	REQUIRE(one.size() == 1);
	CHECK(one[0] == 0);

	const Vector<uint32_t> none = order_of(Vector<int32_t>());
	CHECK(none.size() == 0);
}

} // namespace TestCompositorLayerOrderSort
