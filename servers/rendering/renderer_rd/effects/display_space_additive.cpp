/**************************************************************************/
/*  display_space_additive.cpp                                            */
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

#include "display_space_additive.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

// Hardcoded test quads, world space. Camera in the test scene sits at +Z looking
// toward -Z, so these z<0 quads face it.
//   A + B: two 0.5 gray quads that partially overlap. The overlap strip should
//          reach pure white (0.5 + 0.5 == 1.0 in gamma space) while the wings
//          stay 0.5 gray -- if the add were happening in linear space the strip
//          would only reach ~0.68. This is the gamma-vs-linear discriminator.
//   C:     a cyan quad placed further away; the test scene parks an opaque wall
//          in front of it, so depth-test should occlude most of it.
struct QuadDef {
	Vector3 center;
	Vector2 half_size;
	Color color;
};

static const QuadDef TEST_QUADS[] = {
	{ Vector3(-0.35f, 0.6f, -2.0f), Vector2(0.5f, 0.5f), Color(0.5f, 0.5f, 0.5f, 1.0f) }, // A
	{ Vector3(0.35f, 0.6f, -2.0f), Vector2(0.5f, 0.5f), Color(0.5f, 0.5f, 0.5f, 1.0f) }, // B
	{ Vector3(0.0f, -0.9f, -3.0f), Vector2(0.6f, 0.6f), Color(0.2f, 0.85f, 1.0f, 1.0f) }, // C (occlusion)
};

static const uint32_t QUAD_COUNT = sizeof(TEST_QUADS) / sizeof(TEST_QUADS[0]);

void DisplaySpaceAdditive::_create_quads() {
	// Vertex buffer: QUAD_COUNT * 4 vertices, 3 floats each (world-space position).
	Vector<uint8_t> vertex_data;
	vertex_data.resize(QUAD_COUNT * 4 * 3 * sizeof(float));
	{
		float *v = reinterpret_cast<float *>(vertex_data.ptrw());
		for (uint32_t i = 0; i < QUAD_COUNT; i++) {
			const QuadDef &q = TEST_QUADS[i];
			const float x0 = q.center.x - q.half_size.x;
			const float x1 = q.center.x + q.half_size.x;
			const float y0 = q.center.y - q.half_size.y;
			const float y1 = q.center.y + q.half_size.y;
			const float z = q.center.z;
			// TL, BL, TR, BR
			const float corners[4][3] = {
				{ x0, y1, z },
				{ x0, y0, z },
				{ x1, y1, z },
				{ x1, y0, z },
			};
			for (int c = 0; c < 4; c++) {
				*v++ = corners[c][0];
				*v++ = corners[c][1];
				*v++ = corners[c][2];
			}
			quad_colors.push_back(q.color);
		}
	}

	vertex_buffer = RD::get_singleton()->vertex_buffer_create(vertex_data.size(), vertex_data);

	Vector<RD::VertexAttribute> attributes;
	RD::VertexAttribute vd;
	vd.location = 0;
	vd.stride = sizeof(float) * 3;
	vd.format = RD::DATA_FORMAT_R32G32B32_SFLOAT;
	attributes.push_back(vd);
	vertex_format = RD::get_singleton()->vertex_format_create(attributes);

	Vector<RID> buffers;
	buffers.push_back(vertex_buffer);
	vertex_array = RD::get_singleton()->vertex_array_create(QUAD_COUNT * 4, vertex_format, buffers);

	// Index buffer: 6 indices per quad (two triangles), absolute into the shared
	// vertex buffer. Cull is disabled, so winding does not matter.
	Vector<uint8_t> index_data;
	index_data.resize(QUAD_COUNT * 6 * sizeof(uint16_t));
	{
		uint16_t *idx = reinterpret_cast<uint16_t *>(index_data.ptrw());
		for (uint32_t i = 0; i < QUAD_COUNT; i++) {
			const uint16_t base = uint16_t(i * 4);
			*idx++ = base + 0;
			*idx++ = base + 1;
			*idx++ = base + 2;
			*idx++ = base + 2;
			*idx++ = base + 1;
			*idx++ = base + 3;
		}
	}
	index_buffer = RD::get_singleton()->index_buffer_create(QUAD_COUNT * 6, RD::INDEX_BUFFER_FORMAT_UINT16, index_data);
	for (uint32_t i = 0; i < QUAD_COUNT; i++) {
		quad_index_arrays.push_back(RD::get_singleton()->index_array_create(index_buffer, i * 6, 6));
	}
}

DisplaySpaceAdditive::DisplaySpaceAdditive() {
	Vector<String> modes;
	modes.push_back("");
	shader.initialize(modes);
	shader_version = shader.version_create();

	// Additive blend into a UNORM target: src * 1 + dst * 1, clamped by the format.
	RD::PipelineColorBlendState::Attachment ba;
	ba.enable_blend = true;
	ba.src_color_blend_factor = RD::BLEND_FACTOR_ONE;
	ba.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
	ba.color_blend_op = RD::BLEND_OP_ADD;
	ba.src_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
	ba.dst_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
	ba.alpha_blend_op = RD::BLEND_OP_ADD;
	RD::PipelineColorBlendState blend_state;
	blend_state.attachments.push_back(ba);

	// Depth-test against the opaque scene depth (reversed-Z => GREATER_OR_EQUAL),
	// but never write depth: additive is commutative, so no sorting is needed.
	RD::PipelineDepthStencilState dss;
	dss.enable_depth_test = true;
	dss.enable_depth_write = false;
	dss.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;

	pipeline.setup(shader.version_get_shader(shader_version, 0), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), dss, blend_state, 0);

	_create_quads();
}

DisplaySpaceAdditive::~DisplaySpaceAdditive() {
	if (vertex_buffer.is_valid()) {
		RD::get_singleton()->free_rid(vertex_buffer); // frees the vertex array dependency
	}
	if (index_buffer.is_valid()) {
		RD::get_singleton()->free_rid(index_buffer); // frees the per-quad index array slices
	}
	shader.version_free(shader_version);
}

void DisplaySpaceAdditive::draw(RID p_dest_fb, const Projection &p_projection, const Transform3D &p_cam_transform) {
	RD::get_singleton()->draw_command_begin_label("Display-space additive (SPIKE)");

	// MVP = correction * projection * view. Quads are already in world space.
	// The correction (Y-flip + reverse-Z remap) matches how the scene wrote its
	// depth buffer, so our fragments depth-test correctly for occlusion and land
	// right-side up in the post-tonemap render target. Mirrors the scene path in
	// RenderSceneDataRD::get_..._projection (correction * cam_projection).
	Projection correction;
	correction.set_depth_correction(true);
	Projection view_projection = correction * p_projection * Projection(p_cam_transform.affine_inverse());

	PushConstant push_constant = {};
	MaterialStorage::store_camera(view_projection, push_constant.mvp);

	RD::FramebufferFormatID fb_format = RD::get_singleton()->framebuffer_get_format(p_dest_fb);
	RID rd_pipeline = pipeline.get_render_pipeline(vertex_format, fb_format);

	// DRAW_DEFAULT_ALL == load existing color + depth (do not clear).
	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_dest_fb, RD::DRAW_DEFAULT_ALL);
	RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, rd_pipeline);
	RD::get_singleton()->draw_list_bind_vertex_array(draw_list, vertex_array);
	for (uint32_t i = 0; i < QUAD_COUNT; i++) {
		push_constant.color[0] = quad_colors[i].r;
		push_constant.color[1] = quad_colors[i].g;
		push_constant.color[2] = quad_colors[i].b;
		push_constant.color[3] = quad_colors[i].a;
		RD::get_singleton()->draw_list_bind_index_array(draw_list, quad_index_arrays[i]);
		RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->draw_list_draw(draw_list, true);
	}
	RD::get_singleton()->draw_list_end();

	RD::get_singleton()->draw_command_end_label();
}
