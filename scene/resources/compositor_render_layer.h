/**************************************************************************/
/*  compositor_render_layer.h                                             */
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

#include "core/io/resource.h"
#include "scene/resources/compositor.h"

// Identity token for a compositor render layer: a held-out target that member
// materials (render_mode compositor_layer) draw into, and a CompositorEffect
// composites back. This resource is *only* identity + declared format/seed/stage;
// it is NOT a policy bag. Size, view_count and depth-source are structural
// invariants owned by the engine (they match the scene render target by
// construction), never fields here. The engine allocates one target per distinct
// resource instance, keyed by the resource's object id.

class CompositorRenderLayer : public Resource {
	GDCLASS(CompositorRenderLayer, Resource);

public:
	// Enumerated color format for the layer target. Mirrors the #7916 compositor
	// buffer-format set. INHERIT_SCENE_COLOR matches the scene's base color format.
	enum Format {
		FORMAT_INHERIT_SCENE_COLOR,
		FORMAT_RGBA8,
		FORMAT_RGB10_A2,
		FORMAT_RGBA16F,
		FORMAT_R8,
		FORMAT_R16UI,
		FORMAT_MAX,
	};

	// How the layer target is seeded before its members draw. v1 ships CLEAR and a
	// bound TEXTURE; SCENE_COLOR is declared but deferred (it needs an engine-written
	// coverage channel to avoid double-exposing the scene — see the proposal).
	enum SeedSource {
		SEED_SOURCE_CLEAR,
		SEED_SOURCE_SCENE_COLOR,
		SEED_SOURCE_TEXTURE,
		SEED_SOURCE_MAX,
	};

private:
	Format format = FORMAT_INHERIT_SCENE_COLOR;
	SeedSource seed_source = SEED_SOURCE_CLEAR;
	CompositorEffect::EffectCallbackType stage = CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT;

protected:
	static void _bind_methods();

public:
	void set_format(Format p_format);
	Format get_format() const;

	void set_seed_source(SeedSource p_seed_source);
	SeedSource get_seed_source() const;

	void set_stage(CompositorEffect::EffectCallbackType p_stage);
	CompositorEffect::EffectCallbackType get_stage() const;

	CompositorRenderLayer();
	~CompositorRenderLayer();
};

VARIANT_ENUM_CAST(CompositorRenderLayer::Format)
VARIANT_ENUM_CAST(CompositorRenderLayer::SeedSource)
