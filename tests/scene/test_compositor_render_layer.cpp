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

	// POST_TRANSPARENT is the only stage honored in this version; setting it round-trips.
	layer->set_stage(CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
	CHECK(layer->get_stage() == CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
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

TEST_CASE("[CompositorRenderLayer] Unsupported earlier stages are rejected, leaving the stage at the honored default") {
	// The held-out pass is drawn at a single fixed point (POST_TRANSPARENT) in this version; earlier
	// stages are declared on the enum but not yet wired through, so set_stage rejects them rather than
	// silently accepting a value the render path would ignore.
	Ref<CompositorRenderLayer> layer = memnew(CompositorRenderLayer);

	ERR_PRINT_OFF;
	layer->set_stage(CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE);
	layer->set_stage(CompositorEffect::EFFECT_CALLBACK_TYPE_MAX);
	ERR_PRINT_ON;

	CHECK(layer->get_stage() == CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT);
}

TEST_CASE("[CompositorRenderLayer] Distinct instances carry distinct identities") {
	// The engine keys one held-out target per resource instance off its object id, so
	// two separate layer resources must never collide onto one target.
	Ref<CompositorRenderLayer> a = memnew(CompositorRenderLayer);
	Ref<CompositorRenderLayer> b = memnew(CompositorRenderLayer);

	CHECK(a->get_instance_id() != b->get_instance_id());
}

TEST_CASE("[CompositorEffect] render_layers declarations round-trip and tolerate duplicate identities") {
	// render_layers is the effect's authoritative declaration of the layers it owns (pushed to the render
	// backend at registration). The property stores what the inspector holds verbatim so it stays editable;
	// validation (dropping duplicate identities / null slots from the pushed set) must never crash the setter.
	Ref<CompositorEffect> effect = memnew(CompositorEffect);
	Ref<CompositorRenderLayer> a = memnew(CompositorRenderLayer);
	Ref<CompositorRenderLayer> b = memnew(CompositorRenderLayer);

	TypedArray<CompositorRenderLayer> layers;
	layers.push_back(a);
	layers.push_back(b);
	effect->set_render_layers(layers);
	CHECK(effect->get_render_layers().size() == 2);

	// Declaring the same layer twice is diagnosed and dropped from the pushed registration; the array is
	// still stored verbatim. Suppress the expected error.
	TypedArray<CompositorRenderLayer> with_dupe;
	with_dupe.push_back(a);
	with_dupe.push_back(a);
	ERR_PRINT_OFF;
	effect->set_render_layers(with_dupe);
	ERR_PRINT_ON;
	CHECK(effect->get_render_layers().size() == 2);

	// A null entry is tolerated as an empty inspector slot (no crash, stored verbatim).
	TypedArray<CompositorRenderLayer> with_null;
	with_null.push_back(a);
	with_null.push_back(Ref<CompositorRenderLayer>());
	effect->set_render_layers(with_null);
	CHECK(effect->get_render_layers().size() == 2);
}

} // namespace TestCompositorRenderLayer
