/**************************************************************************/
/*  yutrel_material.cpp                                                   */
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

#include "yutrel_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_material_settings.h"
#include "servers/rendering/rendering_server_default.h"

using namespace RendererSceneRenderImplementation;

void YutrelMaterial::_bind_methods() {
	BIND_ENUM_CONSTANT(SHADING_MODEL_DEFAULT_LIT);
	BIND_ENUM_CONSTANT(SHADING_MODEL_UNLIT);
	BIND_ENUM_CONSTANT(ALPHA_MODE_OPAQUE);
	BIND_ENUM_CONSTANT(ALPHA_MODE_MASK);
	BIND_ENUM_CONSTANT(ALPHA_MODE_BLEND);
	BIND_ENUM_CONSTANT(BLEND_MODE_MIX);
	BIND_ENUM_CONSTANT(BLEND_MODE_ADD);
	BIND_ENUM_CONSTANT(BLEND_MODE_SUB);
	BIND_ENUM_CONSTANT(BLEND_MODE_MUL);
	BIND_ENUM_CONSTANT(CULL_MODE_BACK);
	BIND_ENUM_CONSTANT(CULL_MODE_FRONT);
	BIND_ENUM_CONSTANT(CULL_MODE_DISABLED);
	BIND_ENUM_CONSTANT(TEXTURE_CHANNEL_RED);
	BIND_ENUM_CONSTANT(TEXTURE_CHANNEL_GREEN);
	BIND_ENUM_CONSTANT(TEXTURE_CHANNEL_BLUE);
	BIND_ENUM_CONSTANT(TEXTURE_CHANNEL_ALPHA);

	ClassDB::bind_method(D_METHOD("set_shading_model", "value"), &YutrelMaterial::set_shading_model);
	ClassDB::bind_method(D_METHOD("get_shading_model"), &YutrelMaterial::get_shading_model);
	ClassDB::bind_method(D_METHOD("set_alpha_mode", "value"), &YutrelMaterial::set_alpha_mode);
	ClassDB::bind_method(D_METHOD("get_alpha_mode"), &YutrelMaterial::get_alpha_mode);
	ClassDB::bind_method(D_METHOD("set_alpha_cutoff", "value"), &YutrelMaterial::set_alpha_cutoff);
	ClassDB::bind_method(D_METHOD("get_alpha_cutoff"), &YutrelMaterial::get_alpha_cutoff);
	ClassDB::bind_method(D_METHOD("set_blend_mode", "value"), &YutrelMaterial::set_blend_mode);
	ClassDB::bind_method(D_METHOD("get_blend_mode"), &YutrelMaterial::get_blend_mode);
	ClassDB::bind_method(D_METHOD("set_cull_mode", "value"), &YutrelMaterial::set_cull_mode);
	ClassDB::bind_method(D_METHOD("get_cull_mode"), &YutrelMaterial::get_cull_mode);
	ClassDB::bind_method(D_METHOD("set_vertex_color_use_as_albedo", "value"), &YutrelMaterial::set_vertex_color_use_as_albedo);
	ClassDB::bind_method(D_METHOD("get_vertex_color_use_as_albedo"), &YutrelMaterial::get_vertex_color_use_as_albedo);
	ClassDB::bind_method(D_METHOD("set_vertex_color_is_srgb", "value"), &YutrelMaterial::set_vertex_color_is_srgb);
	ClassDB::bind_method(D_METHOD("get_vertex_color_is_srgb"), &YutrelMaterial::get_vertex_color_is_srgb);
	ADD_GROUP("Rendering", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shading_model", PROPERTY_HINT_ENUM, "Default Lit,Unlit"), "set_shading_model", "get_shading_model");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "alpha_mode", PROPERTY_HINT_ENUM, "Opaque,Mask,Blend"), "set_alpha_mode", "get_alpha_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alpha_cutoff", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_alpha_cutoff", "get_alpha_cutoff");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_mode", PROPERTY_HINT_ENUM, "Mix,Add,Subtract,Multiply"), "set_blend_mode", "get_blend_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mode", PROPERTY_HINT_ENUM, "Back,Front,Disabled"), "set_cull_mode", "get_cull_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vertex_color_use_as_albedo"), "set_vertex_color_use_as_albedo", "get_vertex_color_use_as_albedo");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vertex_color_is_srgb"), "set_vertex_color_is_srgb", "get_vertex_color_is_srgb");

	ClassDB::bind_method(D_METHOD("set_base_color", "value"), &YutrelMaterial::set_base_color);
	ClassDB::bind_method(D_METHOD("get_base_color"), &YutrelMaterial::get_base_color);
	ClassDB::bind_method(D_METHOD("set_base_color_texture", "value"), &YutrelMaterial::set_base_color_texture);
	ClassDB::bind_method(D_METHOD("get_base_color_texture"), &YutrelMaterial::get_base_color_texture);
	ADD_GROUP("Base Color", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "base_color"), "set_base_color", "get_base_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "base_color_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_base_color_texture", "get_base_color_texture");

	ClassDB::bind_method(D_METHOD("set_metallic", "value"), &YutrelMaterial::set_metallic);
	ClassDB::bind_method(D_METHOD("get_metallic"), &YutrelMaterial::get_metallic);
	ClassDB::bind_method(D_METHOD("set_metallic_texture", "value"), &YutrelMaterial::set_metallic_texture);
	ClassDB::bind_method(D_METHOD("get_metallic_texture"), &YutrelMaterial::get_metallic_texture);
	ClassDB::bind_method(D_METHOD("set_metallic_channel", "value"), &YutrelMaterial::set_metallic_channel);
	ClassDB::bind_method(D_METHOD("get_metallic_channel"), &YutrelMaterial::get_metallic_channel);
	ADD_GROUP("Metallic", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "metallic", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_metallic", "get_metallic");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "metallic_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_metallic_texture", "get_metallic_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "metallic_channel", PROPERTY_HINT_ENUM, "Red,Green,Blue,Alpha"), "set_metallic_channel", "get_metallic_channel");

	ClassDB::bind_method(D_METHOD("set_roughness", "value"), &YutrelMaterial::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &YutrelMaterial::get_roughness);
	ClassDB::bind_method(D_METHOD("set_roughness_texture", "value"), &YutrelMaterial::set_roughness_texture);
	ClassDB::bind_method(D_METHOD("get_roughness_texture"), &YutrelMaterial::get_roughness_texture);
	ClassDB::bind_method(D_METHOD("set_roughness_channel", "value"), &YutrelMaterial::set_roughness_channel);
	ClassDB::bind_method(D_METHOD("get_roughness_channel"), &YutrelMaterial::get_roughness_channel);
	ADD_GROUP("Roughness", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "roughness_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_roughness_texture", "get_roughness_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "roughness_channel", PROPERTY_HINT_ENUM, "Red,Green,Blue,Alpha"), "set_roughness_channel", "get_roughness_channel");

	ClassDB::bind_method(D_METHOD("set_specular", "value"), &YutrelMaterial::set_specular);
	ClassDB::bind_method(D_METHOD("get_specular"), &YutrelMaterial::get_specular);
	ADD_GROUP("Specular", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specular", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_specular", "get_specular");

	ClassDB::bind_method(D_METHOD("set_normal_texture", "value"), &YutrelMaterial::set_normal_texture);
	ClassDB::bind_method(D_METHOD("get_normal_texture"), &YutrelMaterial::get_normal_texture);
	ClassDB::bind_method(D_METHOD("set_normal_scale", "value"), &YutrelMaterial::set_normal_scale);
	ClassDB::bind_method(D_METHOD("get_normal_scale"), &YutrelMaterial::get_normal_scale);
	ADD_GROUP("Normal", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_normal_texture", "get_normal_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_scale", PROPERTY_HINT_RANGE, "-16,16,0.01,or_less,or_greater"), "set_normal_scale", "get_normal_scale");

	ClassDB::bind_method(D_METHOD("set_occlusion_texture", "value"), &YutrelMaterial::set_occlusion_texture);
	ClassDB::bind_method(D_METHOD("get_occlusion_texture"), &YutrelMaterial::get_occlusion_texture);
	ClassDB::bind_method(D_METHOD("set_occlusion_strength", "value"), &YutrelMaterial::set_occlusion_strength);
	ClassDB::bind_method(D_METHOD("get_occlusion_strength"), &YutrelMaterial::get_occlusion_strength);
	ClassDB::bind_method(D_METHOD("set_occlusion_channel", "value"), &YutrelMaterial::set_occlusion_channel);
	ClassDB::bind_method(D_METHOD("get_occlusion_channel"), &YutrelMaterial::get_occlusion_channel);
	ADD_GROUP("Occlusion", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "occlusion_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_occlusion_texture", "get_occlusion_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_occlusion_strength", "get_occlusion_strength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_channel", PROPERTY_HINT_ENUM, "Red,Green,Blue,Alpha"), "set_occlusion_channel", "get_occlusion_channel");

	ClassDB::bind_method(D_METHOD("set_emissive_color", "value"), &YutrelMaterial::set_emissive_color);
	ClassDB::bind_method(D_METHOD("get_emissive_color"), &YutrelMaterial::get_emissive_color);
	ClassDB::bind_method(D_METHOD("set_emissive_strength", "value"), &YutrelMaterial::set_emissive_strength);
	ClassDB::bind_method(D_METHOD("get_emissive_strength"), &YutrelMaterial::get_emissive_strength);
	ClassDB::bind_method(D_METHOD("set_emissive_texture", "value"), &YutrelMaterial::set_emissive_texture);
	ClassDB::bind_method(D_METHOD("get_emissive_texture"), &YutrelMaterial::get_emissive_texture);
	ADD_GROUP("Emissive", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "emissive_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_emissive_color", "get_emissive_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "emissive_strength", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_emissive_strength", "get_emissive_strength");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "emissive_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_emissive_texture", "get_emissive_texture");

	ClassDB::bind_method(D_METHOD("set_uv_scale", "value"), &YutrelMaterial::set_uv_scale);
	ClassDB::bind_method(D_METHOD("get_uv_scale"), &YutrelMaterial::get_uv_scale);
	ClassDB::bind_method(D_METHOD("set_uv_offset", "value"), &YutrelMaterial::set_uv_offset);
	ClassDB::bind_method(D_METHOD("get_uv_offset"), &YutrelMaterial::get_uv_offset);
	ADD_GROUP("UV", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "uv_scale"), "set_uv_scale", "get_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "uv_offset"), "set_uv_offset", "get_uv_offset");
}

void YutrelMaterial::set_render_priority(int p_priority) {
	Material::set_render_priority(p_priority);
	_publish();
}

void YutrelMaterial::set_shading_model(ShadingModel p_value) {
	ERR_FAIL_COND(p_value < SHADING_MODEL_DEFAULT_LIT || p_value > SHADING_MODEL_UNLIT);
	shading_model = p_value;
	_publish();
}

void YutrelMaterial::set_alpha_mode(AlphaMode p_value) {
	ERR_FAIL_COND(p_value < ALPHA_MODE_OPAQUE || p_value > ALPHA_MODE_BLEND);
	alpha_mode = p_value;
	_publish();
}

void YutrelMaterial::set_alpha_cutoff(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	alpha_cutoff = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelMaterial::set_blend_mode(BlendMode p_value) {
	ERR_FAIL_COND(p_value < BLEND_MODE_MIX || p_value > BLEND_MODE_MUL);
	blend_mode = p_value;
	_publish();
}

void YutrelMaterial::set_cull_mode(CullMode p_value) {
	ERR_FAIL_COND(p_value < CULL_MODE_BACK || p_value > CULL_MODE_DISABLED);
	cull_mode = p_value;
	_publish();
}

void YutrelMaterial::set_vertex_color_use_as_albedo(bool p_value) {
	vertex_color_use_as_albedo = p_value;
	_publish();
}

void YutrelMaterial::set_vertex_color_is_srgb(bool p_value) {
	vertex_color_is_srgb = p_value;
	_publish();
}

void YutrelMaterial::set_base_color(const Color &p_value) {
	base_color = p_value;
	_publish();
}

void YutrelMaterial::set_base_color_texture(const Ref<Texture2D> &p_value) {
	base_color_texture = p_value;
	_publish();
}

void YutrelMaterial::set_metallic(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	metallic = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelMaterial::set_metallic_texture(const Ref<Texture2D> &p_value) {
	metallic_texture = p_value;
	_publish();
}

void YutrelMaterial::set_metallic_channel(TextureChannel p_value) {
	ERR_FAIL_COND(p_value < TEXTURE_CHANNEL_RED || p_value > TEXTURE_CHANNEL_ALPHA);
	metallic_channel = p_value;
	_publish();
}

void YutrelMaterial::set_roughness(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	roughness = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelMaterial::set_roughness_texture(const Ref<Texture2D> &p_value) {
	roughness_texture = p_value;
	_publish();
}

void YutrelMaterial::set_roughness_channel(TextureChannel p_value) {
	ERR_FAIL_COND(p_value < TEXTURE_CHANNEL_RED || p_value > TEXTURE_CHANNEL_ALPHA);
	roughness_channel = p_value;
	_publish();
}

void YutrelMaterial::set_specular(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	specular = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelMaterial::set_normal_texture(const Ref<Texture2D> &p_value) {
	normal_texture = p_value;
	_publish();
}

void YutrelMaterial::set_normal_scale(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	normal_scale = p_value;
	_publish();
}

void YutrelMaterial::set_occlusion_texture(const Ref<Texture2D> &p_value) {
	occlusion_texture = p_value;
	_publish();
}

void YutrelMaterial::set_occlusion_strength(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	occlusion_strength = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelMaterial::set_occlusion_channel(TextureChannel p_value) {
	ERR_FAIL_COND(p_value < TEXTURE_CHANNEL_RED || p_value > TEXTURE_CHANNEL_ALPHA);
	occlusion_channel = p_value;
	_publish();
}

void YutrelMaterial::set_emissive_color(const Color &p_value) {
	emissive_color = p_value;
	_publish();
}

void YutrelMaterial::set_emissive_strength(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	emissive_strength = MAX(0.0f, p_value);
	_publish();
}

void YutrelMaterial::set_emissive_texture(const Ref<Texture2D> &p_value) {
	emissive_texture = p_value;
	_publish();
}

void YutrelMaterial::set_uv_scale(const Vector2 &p_value) {
	uv_scale = p_value;
	_publish();
}

void YutrelMaterial::set_uv_offset(const Vector2 &p_value) {
	uv_offset = p_value;
	_publish();
}

void YutrelMaterial::_publish() {
	YutrelMaterialSettings settings;
	YutrelMaterialData &data = settings.data;
	const Color linear_base = base_color.srgb_to_linear();
	const Color linear_emission = emissive_color.srgb_to_linear();
	for (int i = 0; i < 4; i++) {
		data.albedo[i] = linear_base[i];
		data.metallic_channel[i] = i == metallic_channel;
		data.roughness_channel[i] = i == roughness_channel;
		data.ao_channel[i] = i == occlusion_channel;
	}
	for (int i = 0; i < 3; i++) {
		data.emission[i] = linear_emission[i];
	}
	data.emission_energy = emissive_strength;
	data.uv_transform[0] = uv_scale.x;
	data.uv_transform[1] = uv_scale.y;
	data.uv_transform[2] = uv_offset.x;
	data.uv_transform[3] = uv_offset.y;
	data.roughness = roughness;
	data.metallic = metallic;
	data.specular = specular;
	data.normal_scale = normal_scale;
	data.alpha_scissor_threshold = alpha_cutoff;
	data.ao_strength = occlusion_strength;
	data.blend_mode = blend_mode;
	data.flags = YutrelMaterialData::EMISSION_OP_MULTIPLY;
	if (shading_model == SHADING_MODEL_UNLIT) {
		data.flags |= YutrelMaterialData::UNLIT;
	}
	if (alpha_mode == ALPHA_MODE_BLEND || blend_mode != BLEND_MODE_MIX) {
		data.flags |= YutrelMaterialData::TRANSPARENT;
	}
	if (alpha_mode == ALPHA_MODE_MASK && blend_mode == BLEND_MODE_MIX) {
		data.flags |= YutrelMaterialData::ALPHA_SCISSOR;
	}
	if (cull_mode == CULL_MODE_DISABLED) {
		data.flags |= YutrelMaterialData::DOUBLE_SIDED;
	}
	if (cull_mode == CULL_MODE_FRONT) {
		data.flags |= YutrelMaterialData::CULL_FRONT;
	}
	if (vertex_color_use_as_albedo) {
		data.flags |= YutrelMaterialData::VERTEX_COLOR;
	}
	if (vertex_color_is_srgb) {
		data.flags |= YutrelMaterialData::SRGB_VERTEX_COLOR;
	}
	if (normal_texture.is_valid()) {
		data.flags |= YutrelMaterialData::HAS_NORMAL_MAP;
	}
	if (emissive_texture.is_valid()) {
		data.flags |= YutrelMaterialData::HAS_EMISSION_TEXTURE;
	}

	settings.textures[YutrelMaterialBridge::ALBEDO] = base_color_texture.is_valid() ? base_color_texture->get_rid() : RID();
	settings.textures[YutrelMaterialBridge::NORMAL] = normal_texture.is_valid() ? normal_texture->get_rid() : RID();
	settings.textures[YutrelMaterialBridge::ROUGHNESS] = roughness_texture.is_valid() ? roughness_texture->get_rid() : RID();
	settings.textures[YutrelMaterialBridge::METALLIC] = metallic_texture.is_valid() ? metallic_texture->get_rid() : RID();
	settings.textures[YutrelMaterialBridge::AO] = occlusion_texture.is_valid() ? occlusion_texture->get_rid() : RID();
	settings.textures[YutrelMaterialBridge::EMISSION] = emissive_texture.is_valid() ? emissive_texture->get_rid() : RID();
	settings.render_priority = get_render_priority();
	YutrelMaterialSettings::set(get_rid(), settings);
	emit_changed();
	RenderingServerDefault::redraw_request();
}

YutrelMaterial::YutrelMaterial() {
	_set_material(RS::get_singleton()->material_create());
	_mark_ready();
	_publish();
}

YutrelMaterial::~YutrelMaterial() {
	const RID rid = get_rid();
	YutrelMaterialSettings::erase(rid);
	if (RS::get_singleton()) {
		RS::get_singleton()->free_rid(rid);
	}
	_set_material(RID());
}
