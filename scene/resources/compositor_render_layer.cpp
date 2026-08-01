/**************************************************************************/
/*  compositor_render_layer.cpp                                           */
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

#include "compositor_render_layer.h"

#include "core/object/class_db.h"

void CompositorRenderLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_format", "format"), &CompositorRenderLayer::set_format);
	ClassDB::bind_method(D_METHOD("get_format"), &CompositorRenderLayer::get_format);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "format", PROPERTY_HINT_ENUM, "Inherit Scene Color,RGBA8,RGB10A2,RGBA16F,R8,R16UI"), "set_format", "get_format");

	ClassDB::bind_method(D_METHOD("set_seed_source", "seed_source"), &CompositorRenderLayer::set_seed_source);
	ClassDB::bind_method(D_METHOD("get_seed_source"), &CompositorRenderLayer::get_seed_source);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed_source", PROPERTY_HINT_ENUM, "Clear,Scene Color,Texture"), "set_seed_source", "get_seed_source");

	ClassDB::bind_method(D_METHOD("set_stage", "stage"), &CompositorRenderLayer::set_stage);
	ClassDB::bind_method(D_METHOD("get_stage"), &CompositorRenderLayer::get_stage);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stage", PROPERTY_HINT_ENUM, "Pre Opaque,Post Opaque,Post Sky,Pre Transparent,Post Transparent"), "set_stage", "get_stage");

	BIND_ENUM_CONSTANT(FORMAT_INHERIT_SCENE_COLOR);
	BIND_ENUM_CONSTANT(FORMAT_RGBA8);
	BIND_ENUM_CONSTANT(FORMAT_RGB10_A2);
	BIND_ENUM_CONSTANT(FORMAT_RGBA16F);
	BIND_ENUM_CONSTANT(FORMAT_R8);
	BIND_ENUM_CONSTANT(FORMAT_R16UI);
	BIND_ENUM_CONSTANT(FORMAT_MAX);

	BIND_ENUM_CONSTANT(SEED_SOURCE_CLEAR);
	BIND_ENUM_CONSTANT(SEED_SOURCE_SCENE_COLOR);
	BIND_ENUM_CONSTANT(SEED_SOURCE_TEXTURE);
	BIND_ENUM_CONSTANT(SEED_SOURCE_MAX);
}

void CompositorRenderLayer::set_format(Format p_format) {
	ERR_FAIL_INDEX(p_format, FORMAT_MAX);
	if (format == p_format) {
		return;
	}
	format = p_format;
	emit_changed();
}

CompositorRenderLayer::Format CompositorRenderLayer::get_format() const {
	return format;
}

void CompositorRenderLayer::set_seed_source(SeedSource p_seed_source) {
	ERR_FAIL_INDEX(p_seed_source, SEED_SOURCE_MAX);
	if (seed_source == p_seed_source) {
		return;
	}
	seed_source = p_seed_source;
	emit_changed();
}

CompositorRenderLayer::SeedSource CompositorRenderLayer::get_seed_source() const {
	return seed_source;
}

void CompositorRenderLayer::set_stage(CompositorEffect::EffectCallbackType p_stage) {
	ERR_FAIL_INDEX(p_stage, CompositorEffect::EFFECT_CALLBACK_TYPE_MAX);
	stage = p_stage;
}

CompositorEffect::EffectCallbackType CompositorRenderLayer::get_stage() const {
	return stage;
}

CompositorRenderLayer::CompositorRenderLayer() {
}

CompositorRenderLayer::~CompositorRenderLayer() {
}
