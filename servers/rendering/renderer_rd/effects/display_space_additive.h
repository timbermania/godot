/**************************************************************************/
/*  display_space_additive.h                                              */
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

// SPIKE 2: hardcoded proof-of-concept for display-space (gamma-space) additive
// blending in Forward+. Draws a fixed set of test quads into the post-tonemap
// UNORM render target with hardware additive blend + depth-test against the
// scene depth. NOT a shippable feature -- it exists only to prove the mechanism
// (gamma-space add, clamp at 1.0, correct occlusion). See
// docs/display-space-additive-blending.md.

#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/display_space_additive.glsl.gen.h"

namespace RendererRD {

class DisplaySpaceAdditive {
private:
	struct PushConstant {
		float mvp[16];
		float color[4];
	};

	RD::VertexFormatID vertex_format = 0;
	RID vertex_buffer;
	RID vertex_array;
	RID index_buffer;
	Vector<RID> quad_index_arrays; // one 6-index slice per quad
	Vector<Color> quad_colors;

	DisplaySpaceAdditiveShaderRD shader;
	RID shader_version;
	PipelineCacheRD pipeline;

	void _create_quads();

public:
	DisplaySpaceAdditive();
	~DisplaySpaceAdditive();

	// Draws the test quads into p_dest_fb (must be [display color + scene depth]).
	void draw(RID p_dest_fb, const Projection &p_projection, const Transform3D &p_cam_transform);
};

} // namespace RendererRD
