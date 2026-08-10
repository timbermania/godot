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
#include "scene/resources/texture.h"

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
	// Enumerated color format for the layer target. INHERIT_SCENE_COLOR matches the scene's base color
	// format (the sane default, since members shade through their real material); RGBA8/RGBA16F cover the
	// LDR/HDR cases. More formats can be added when a use case needs one.
	enum Format {
		FORMAT_INHERIT_SCENE_COLOR,
		FORMAT_RGBA8,
		FORMAT_RGBA16F,
		FORMAT_MAX,
	};

	// How the layer target is seeded before its members draw. v1 ships CLEAR and a
	// bound TEXTURE. A SCENE_COLOR seed is a named future extension, deliberately not
	// exposed here yet: it needs an engine-written coverage channel to avoid
	// double-exposing the scene (see the proposal), so shipping the enum value before
	// the engine path exists would be a non-functional public surface.
	enum SeedSource {
		SEED_SOURCE_CLEAR,
		SEED_SOURCE_TEXTURE,
		SEED_SOURCE_MAX,
	};

private:
	Format format = FORMAT_INHERIT_SCENE_COLOR;
	SeedSource seed_source = SEED_SOURCE_CLEAR;
	CompositorEffect::EffectCallbackType stage = CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT;
	// Only meaningful when seed_source == SEED_SOURCE_TEXTURE. The engine copies this texture into the
	// engine-owned layer target before its members draw, so a fold's sub/mix modes can read the seed in
	// place. May be a live, per-frame-updated RD-backed texture (e.g. Texture2DRD): the render thread
	// resolves the current RD texture from this resource's stable RID at pass time.
	Ref<Texture2D> seed_texture;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_format(Format p_format);
	Format get_format() const;

	void set_seed_source(SeedSource p_seed_source);
	SeedSource get_seed_source() const;

	void set_stage(CompositorEffect::EffectCallbackType p_stage);
	CompositorEffect::EffectCallbackType get_stage() const;

	void set_seed_texture(const Ref<Texture2D> &p_seed_texture);
	Ref<Texture2D> get_seed_texture() const;

	CompositorRenderLayer();
	~CompositorRenderLayer();
};

VARIANT_ENUM_CAST(CompositorRenderLayer::Format)
VARIANT_ENUM_CAST(CompositorRenderLayer::SeedSource)
