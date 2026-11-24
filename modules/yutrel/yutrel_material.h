/**************************************************************************/
/*  yutrel_material.h                                                     */
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

#include "scene/resources/material.h"
#include "scene/resources/texture.h"

class YutrelMaterial : public Material {
	GDCLASS(YutrelMaterial, Material);

public:
	enum ShadingModel {
		SHADING_MODEL_DEFAULT_LIT,
		SHADING_MODEL_UNLIT,
	};

	enum AlphaMode {
		ALPHA_MODE_OPAQUE,
		ALPHA_MODE_MASK,
		ALPHA_MODE_BLEND,
	};

	enum BlendMode {
		BLEND_MODE_MIX,
		BLEND_MODE_ADD,
		BLEND_MODE_SUB,
		BLEND_MODE_MUL,
	};

	enum CullMode {
		CULL_MODE_BACK,
		CULL_MODE_FRONT,
		CULL_MODE_DISABLED,
	};

	enum TextureChannel {
		TEXTURE_CHANNEL_RED,
		TEXTURE_CHANNEL_GREEN,
		TEXTURE_CHANNEL_BLUE,
		TEXTURE_CHANNEL_ALPHA,
	};

private:
	ShadingModel shading_model = SHADING_MODEL_DEFAULT_LIT;
	AlphaMode alpha_mode = ALPHA_MODE_OPAQUE;
	float alpha_cutoff = 0.5f;
	BlendMode blend_mode = BLEND_MODE_MIX;
	CullMode cull_mode = CULL_MODE_BACK;
	bool vertex_color_use_as_albedo = false;
	bool vertex_color_is_srgb = false;
	Color base_color = Color(1, 1, 1, 1);
	Ref<Texture2D> base_color_texture;
	float metallic = 0.0f;
	Ref<Texture2D> metallic_texture;
	TextureChannel metallic_channel = TEXTURE_CHANNEL_BLUE;
	float roughness = 1.0f;
	Ref<Texture2D> roughness_texture;
	TextureChannel roughness_channel = TEXTURE_CHANNEL_GREEN;
	float specular = 0.5f;
	Ref<Texture2D> normal_texture;
	float normal_scale = 1.0f;
	Ref<Texture2D> occlusion_texture;
	float occlusion_strength = 1.0f;
	TextureChannel occlusion_channel = TEXTURE_CHANNEL_RED;
	Color emissive_color = Color(0, 0, 0);
	float emissive_strength = 1.0f;
	Ref<Texture2D> emissive_texture;
	Vector2 uv_scale = Vector2(1, 1);
	Vector2 uv_offset;

	void _publish();

protected:
	static void _bind_methods();
	bool _can_use_render_priority() const override { return true; }
	bool _can_do_next_pass() const override { return false; }

public:
	Shader::Mode get_shader_mode() const override { return Shader::MODE_SPATIAL; }
	RID get_shader_rid() const override { return RID(); }
	void set_render_priority(int p_priority) override;

	void set_shading_model(ShadingModel p_value);
	ShadingModel get_shading_model() const { return shading_model; }
	void set_alpha_mode(AlphaMode p_value);
	AlphaMode get_alpha_mode() const { return alpha_mode; }
	void set_alpha_cutoff(float p_value);
	float get_alpha_cutoff() const { return alpha_cutoff; }
	void set_blend_mode(BlendMode p_value);
	BlendMode get_blend_mode() const { return blend_mode; }
	void set_cull_mode(CullMode p_value);
	CullMode get_cull_mode() const { return cull_mode; }
	void set_vertex_color_use_as_albedo(bool p_value);
	bool get_vertex_color_use_as_albedo() const { return vertex_color_use_as_albedo; }
	void set_vertex_color_is_srgb(bool p_value);
	bool get_vertex_color_is_srgb() const { return vertex_color_is_srgb; }

	void set_base_color(const Color &p_value);
	Color get_base_color() const { return base_color; }
	void set_base_color_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_base_color_texture() const { return base_color_texture; }

	void set_metallic(float p_value);
	float get_metallic() const { return metallic; }
	void set_metallic_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_metallic_texture() const { return metallic_texture; }
	void set_metallic_channel(TextureChannel p_value);
	TextureChannel get_metallic_channel() const { return metallic_channel; }

	void set_roughness(float p_value);
	float get_roughness() const { return roughness; }
	void set_roughness_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_roughness_texture() const { return roughness_texture; }
	void set_roughness_channel(TextureChannel p_value);
	TextureChannel get_roughness_channel() const { return roughness_channel; }

	void set_specular(float p_value);
	float get_specular() const { return specular; }

	void set_normal_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_normal_texture() const { return normal_texture; }
	void set_normal_scale(float p_value);
	float get_normal_scale() const { return normal_scale; }

	void set_occlusion_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_occlusion_texture() const { return occlusion_texture; }
	void set_occlusion_strength(float p_value);
	float get_occlusion_strength() const { return occlusion_strength; }
	void set_occlusion_channel(TextureChannel p_value);
	TextureChannel get_occlusion_channel() const { return occlusion_channel; }

	void set_emissive_color(const Color &p_value);
	Color get_emissive_color() const { return emissive_color; }
	void set_emissive_strength(float p_value);
	float get_emissive_strength() const { return emissive_strength; }
	void set_emissive_texture(const Ref<Texture2D> &p_value);
	Ref<Texture2D> get_emissive_texture() const { return emissive_texture; }

	void set_uv_scale(const Vector2 &p_value);
	Vector2 get_uv_scale() const { return uv_scale; }
	void set_uv_offset(const Vector2 &p_value);
	Vector2 get_uv_offset() const { return uv_offset; }

	YutrelMaterial();
	~YutrelMaterial();
};

VARIANT_ENUM_CAST(YutrelMaterial::ShadingModel);
VARIANT_ENUM_CAST(YutrelMaterial::AlphaMode);
VARIANT_ENUM_CAST(YutrelMaterial::BlendMode);
VARIANT_ENUM_CAST(YutrelMaterial::CullMode);
VARIANT_ENUM_CAST(YutrelMaterial::TextureChannel);
