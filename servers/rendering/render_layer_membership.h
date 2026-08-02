/**************************************************************************/
/*  render_layer_membership.h                                             */
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

#include "core/object/object_id.h"
#include "core/templates/rid.h"

// A GeometryInstance3D's compositor render-layer membership, pushed down from its
// `CompositorRenderLayer` resource to the render backend on the main thread (so the render thread
// never dereferences the resource). This collapses what used to travel as a five-parameter tuple and
// five loose per-instance fields into one value passed and stored by name.
//
// `format`/`seed_source` MIRROR `CompositorRenderLayer::Format`/`SeedSource`. The enums are duplicated
// here rather than included from `scene/` on purpose: the render server must not take a header
// dependency on the scene layer (that is the wrong direction, and would invert Godot's scene->servers
// layering). `scene/3d/visual_instance_3d.cpp` — which sees both — `static_assert`s that the two
// enumerations stay in lockstep, so this mirror can never silently drift.
struct RenderLayerMembership {
	// Mirror of CompositorRenderLayer::Format (kept value-for-value in sync; see static_asserts).
	enum Format {
		FORMAT_INHERIT_SCENE_COLOR,
		FORMAT_RGBA8,
		FORMAT_RGB10_A2,
		FORMAT_RGBA16F,
		FORMAT_R8,
		FORMAT_R16UI,
		FORMAT_MAX,
	};

	// Mirror of CompositorRenderLayer::SeedSource (kept value-for-value in sync; see static_asserts).
	enum SeedSource {
		SEED_SOURCE_CLEAR,
		SEED_SOURCE_TEXTURE,
		SEED_SOURCE_MAX,
	};

	// Identity of the instance's `CompositorRenderLayer` resource; invalid == not a member.
	ObjectID layer_id;
	// Exact caller-order key the RENDER_LIST_COMPOSITOR_LAYER list sorts by (ties: submission order).
	int32_t order = 0;
	Format format = FORMAT_INHERIT_SCENE_COLOR;
	SeedSource seed_source = SEED_SOURCE_CLEAR;
	// Seed texture's RenderingServer RID (resolved on the main thread); only meaningful when
	// `seed_source == SEED_SOURCE_TEXTURE`. The pass resolves it to the current RD texture at draw time.
	RID seed_texture;

	bool is_member() const { return layer_id.is_valid(); }
};
