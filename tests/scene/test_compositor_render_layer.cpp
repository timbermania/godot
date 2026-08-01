/**************************************************************************/
/*  test_compositor_render_layer.cpp                                      */
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

#include "scene/resources/compositor_render_layer.h"

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_compositor_render_layer)

namespace TestCompositorRenderLayer {

TEST_CASE("[CompositorRenderLayer] Defaults are the minimal, honest identity token") {
	Ref<CompositorRenderLayer> layer = memnew(CompositorRenderLayer);

	// The identity token ships with the safe, self-contained defaults: inherit the
	// scene color format, clear before drawing, composite after the transparent pass.
	CHECK(layer->get_format() == CompositorRenderLayer::FORMAT_INHERIT_SCENE_COLOR);
	CHECK(layer->get_seed_source() == CompositorRenderLayer::SEED_SOURCE_CLEAR);
	CHECK(layer->get_stage() == CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
}

TEST_CASE("[CompositorRenderLayer] Declarations round-trip through setters") {
	Ref<CompositorRenderLayer> layer = memnew(CompositorRenderLayer);

	layer->set_format(CompositorRenderLayer::FORMAT_RGBA16F);
	CHECK(layer->get_format() == CompositorRenderLayer::FORMAT_RGBA16F);

	layer->set_seed_source(CompositorRenderLayer::SEED_SOURCE_TEXTURE);
	CHECK(layer->get_seed_source() == CompositorRenderLayer::SEED_SOURCE_TEXTURE);

	layer->set_stage(CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE);
	CHECK(layer->get_stage() == CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE);
}

TEST_CASE("[CompositorRenderLayer] Out-of-range declarations are rejected, leaving the value untouched") {
	Ref<CompositorRenderLayer> layer = memnew(CompositorRenderLayer);

	ERR_PRINT_OFF;
	layer->set_format(CompositorRenderLayer::FORMAT_MAX);
	layer->set_seed_source(CompositorRenderLayer::SEED_SOURCE_MAX);
	ERR_PRINT_ON;

	CHECK(layer->get_format() == CompositorRenderLayer::FORMAT_INHERIT_SCENE_COLOR);
	CHECK(layer->get_seed_source() == CompositorRenderLayer::SEED_SOURCE_CLEAR);
}

TEST_CASE("[CompositorRenderLayer] Distinct instances carry distinct identities") {
	// The engine keys one held-out target per resource instance off its object id, so
	// two separate layer resources must never collide onto one target.
	Ref<CompositorRenderLayer> a = memnew(CompositorRenderLayer);
	Ref<CompositorRenderLayer> b = memnew(CompositorRenderLayer);

	CHECK(a->get_instance_id() != b->get_instance_id());
}

} // namespace TestCompositorRenderLayer
