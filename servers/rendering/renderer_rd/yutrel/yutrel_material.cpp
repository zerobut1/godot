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

#include "yutrel_material_settings.h"

#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

namespace {

using MaterialStorage = RendererRD::MaterialStorage;
using TextureStorage = RendererRD::TextureStorage;

struct BridgeShaderData : MaterialStorage::ShaderData {
	using Bridge = YutrelMaterialBridge;
	Bridge::BlendMode blend_mode = Bridge::BLEND_MIX;
	bool standard = false;
	bool orm = false;
	bool transparent = false;
	bool normal_enabled = false;
	bool ao_enabled = false;
	bool emission_enabled = false;
	uint32_t flags = 0;
	Vector4 roughness_channel;
#ifdef TOOLS_ENABLED
	YutrelMaterialBridge::GizmoStyle gizmo;
#endif

	void set_code(const String &p_code) override {
		using Data = YutrelMaterialData;
		// BaseMaterial3D exposes feature switches through its generated shader,
		// while its scalar and texture values remain regular MaterialStorage parameters.
		const bool generated = p_code.contains("Shader automatically converted from");
		orm = generated && p_code.contains("ORMMaterial3D.");
		standard = orm || (generated && p_code.contains("StandardMaterial3D."));
		const bool scissor = p_code.contains("ALPHA_SCISSOR_THRESHOLD =");
		blend_mode = Bridge::BLEND_MIX;
		if (p_code.contains("blend_add")) {
			blend_mode = Bridge::BLEND_ADD;
		} else if (p_code.contains("blend_sub")) {
			blend_mode = Bridge::BLEND_SUB;
		} else if (p_code.contains("blend_mul")) {
			blend_mode = Bridge::BLEND_MUL;
		}
		transparent = (!scissor && (p_code.contains("ALPHA *=") || p_code.contains("ALPHA ="))) || blend_mode != Bridge::BLEND_MIX;
		normal_enabled = p_code.contains("NORMAL_MAP =");
		ao_enabled = p_code.contains("AO_LIGHT_AFFECT =");
		emission_enabled = p_code.contains("uniform float emission_energy");
		const bool vertex_color = p_code.contains("albedo_tex *= COLOR");
		const bool srgb_vertex_color = p_code.contains("Vertex Color is sRGB: Enabled");
		flags = 0;
		if (scissor) {
			flags |= Data::ALPHA_SCISSOR;
		}
		if (p_code.contains("cull_disabled")) {
			flags |= Data::DOUBLE_SIDED;
		} else if (p_code.contains("cull_front")) {
			flags |= Data::CULL_FRONT;
		}
		if (vertex_color) {
			flags |= Data::VERTEX_COLOR;
		}
		if (srgb_vertex_color) {
			flags |= Data::SRGB_VERTEX_COLOR;
		}
		if (p_code.contains("emission.rgb * emission_tex")) {
			flags |= Data::EMISSION_OP_MULTIPLY;
		}
		if (p_code.contains(", unshaded")) {
			flags |= Data::UNLIT;
		}

		// StandardMaterial3D specializes this mask in the shader instead of setting a uniform.
		roughness_channel = Vector4(1, 0, 0, 0);
		if (p_code.contains("hint_roughness_g,")) {
			roughness_channel = Vector4(0, 1, 0, 0);
		} else if (p_code.contains("hint_roughness_b,")) {
			roughness_channel = Vector4(0, 0, 1, 0);
		} else if (p_code.contains("hint_roughness_a,")) {
			roughness_channel = Vector4(0, 0, 0, 1);
		} else if (p_code.contains("hint_roughness_gray,")) {
			roughness_channel = Vector4(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0, 0);
		}

#ifdef TOOLS_ENABLED
		gizmo = {};
		gizmo.on_top = p_code.contains("depth_test_disabled");
		gizmo.alpha_scissor = scissor;
		gizmo.supported = true;
		if (p_code.contains("camera_world_pos_on_plane") && p_code.contains("grid_size")) {
			gizmo.flags = Bridge::GRID | Bridge::VERTEX_COLOR | Bridge::SRGB_VERTEX_COLOR;
		} else if (p_code.contains("screen_point_final") && p_code.contains("clip_final")) {
			gizmo.flags = Bridge::ORIGIN_AXIS;
		} else if (gizmo.on_top && p_code.contains("orientation <= 0.005") && p_code.contains("VERTEX += NORMAL")) {
			gizmo.flags = Bridge::AXIS_RING;
		} else if (gizmo.on_top && p_code.contains("camera_dir_local") && p_code.contains("rotation_matrix")) {
			gizmo.flags = Bridge::VIEW_RING;
		} else if (standard) {
			if (vertex_color) {
				gizmo.flags |= Bridge::VERTEX_COLOR;
			}
			if (srgb_vertex_color) {
				gizmo.flags |= Bridge::SRGB_VERTEX_COLOR;
			}
			if (p_code.contains("Billboard Mode: Enabled") || p_code.contains("Billboard Mode: Y-Billboard")) {
				gizmo.flags |= Bridge::BILLBOARD;
			}
			if (p_code.contains("Fixed Size: Enabled")) {
				gizmo.flags |= Bridge::FIXED_SIZE;
			}
			if (p_code.contains("POINT_SIZE = point_size")) {
				gizmo.flags |= Bridge::POINT_SPRITE;
			}
		} else {
			gizmo.supported = false;
		}
#endif
	}

	bool is_animated() const override { return false; }
	bool casts_shadows() const override { return true; }
	RenderingServerTypes::ShaderNativeSourceCode get_native_source_code() const override { return {}; }
	Pair<ShaderRD *, RID> get_native_shader_and_version() const override { return {}; }
};

struct BridgeMaterialData : MaterialStorage::MaterialData {
	const BridgeShaderData *shader;
	int32_t priority = 0;

	explicit BridgeMaterialData(MaterialStorage::ShaderData *p_shader) :
			shader(static_cast<const BridgeShaderData *>(p_shader)) {}
	void set_render_priority(int p_priority) override { priority = p_priority; }
	void set_next_pass(RID p_pass) override {}
	bool update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) override { return false; }
};

const BridgeMaterialData *get_bridge_data(RID p_material) {
	return static_cast<const BridgeMaterialData *>(MaterialStorage::get_singleton()->material_get_data(p_material, MaterialStorage::SHADER_TYPE_3D));
}

RID make_fallback(const Vector<uint8_t> &p_pixel, const char *p_name) {
	RD *rd = RD::get_singleton();
	RD::TextureFormat format;
	format.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
	format.width = 1;
	format.height = 1;
	format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
	const RID texture = rd->texture_create(format, {}, Vector<Vector<uint8_t>>{ p_pixel });
	if (texture.is_valid()) {
		rd->set_resource_name(texture, p_name);
	}
	return texture;
}

void store_mask(float (&r_mask)[4], const Vector4 &p_mask) {
	for (int i = 0; i < 4; ++i) {
		r_mask[i] = p_mask[i];
	}
}

} // namespace

YutrelMaterialBridge::YutrelMaterialBridge() {
	auto *materials = MaterialStorage::get_singleton();
	materials->shader_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, []() -> MaterialStorage::ShaderData * {
		return memnew(BridgeShaderData);
	});
	materials->material_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, [](MaterialStorage::ShaderData *p_shader) -> MaterialStorage::MaterialData * {
		return memnew(BridgeMaterialData(p_shader));
	});
}

YutrelMaterialBridge::~YutrelMaterialBridge() {
	auto *materials = MaterialStorage::get_singleton();
	materials->material_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, nullptr);
	materials->shader_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, nullptr);
	RD *rd = RD::get_singleton();
	if (white_texture.is_valid()) {
		rd->free_rid(white_texture);
	}
	if (flat_normal_texture.is_valid()) {
		rd->free_rid(flat_normal_texture);
	}
}

RID YutrelMaterialBridge::get_surface_material(const RenderGeometryInstanceBase &p_instance, RID p_mesh, int p_surface_index) {
	const auto &overrides = *p_instance.data;
	if (overrides.material_override.is_valid()) {
		return overrides.material_override;
	}
	if (p_surface_index < overrides.surface_materials.size() && overrides.surface_materials[p_surface_index].is_valid()) {
		return overrides.surface_materials[p_surface_index];
	}
	return RendererRD::MeshStorage::get_singleton()->mesh_surface_get_material(p_mesh, p_surface_index);
}

#ifdef TOOLS_ENABLED
const YutrelMaterialBridge::GizmoStyle *YutrelMaterialBridge::get_gizmo_style(RID p_material, int32_t &r_priority) {
	const BridgeMaterialData *material = get_bridge_data(p_material);
	if (!material || !material->shader->gizmo.supported) {
		return nullptr;
	}
	r_priority = material->priority;
	return &material->shader->gizmo;
}
#endif

RID YutrelMaterialBridge::get_white_texture() {
	if (white_texture.is_null()) {
		white_texture = make_fallback({ 255, 255, 255, 255 }, "Yutrel White");
	}
	return white_texture;
}

bool YutrelMaterialBridge::get_material(RID p_material, Material &r_material) {
	get_white_texture();
	if (flat_normal_texture.is_null()) {
		flat_normal_texture = make_fallback({ 128, 128, 255, 255 }, "Yutrel Flat Normal");
	}
	ERR_FAIL_COND_V(white_texture.is_null() || flat_normal_texture.is_null(), false);

	for (int i = 0; i < TEXTURE_COUNT; ++i) {
		r_material.textures[i] = i == NORMAL ? flat_normal_texture : white_texture;
	}
	if (p_material.is_null()) {
		return true;
	}
	YutrelMaterialSettings native;
	if (YutrelMaterialSettings::get(p_material, native)) {
		r_material.data = native.data;
		r_material.transparent = (native.data.flags & YutrelMaterialData::TRANSPARENT) != 0;
		r_material.blend_mode = BlendMode(native.data.blend_mode);
		r_material.render_priority = native.render_priority;
		for (int i = 0; i < TEXTURE_COUNT; i++) {
			if (native.textures[i].is_valid()) {
				const RID texture = TextureStorage::get_singleton()->texture_get_rd_texture(native.textures[i], i == ALBEDO || i == EMISSION);
				if (texture.is_valid()) {
					r_material.textures[i] = texture;
				}
			}
		}
		return true;
	}
	const BridgeMaterialData *source = get_bridge_data(p_material);
	if (!source || !source->shader->standard) {
		return false;
	}

	auto *materials = MaterialStorage::get_singleton();
	auto *textures = TextureStorage::get_singleton();
	const BridgeShaderData &shader = *source->shader;
	YutrelMaterialData &data = r_material.data;
	r_material.transparent = shader.transparent;
	r_material.blend_mode = shader.blend_mode;
	r_material.render_priority = source->priority;
	data.flags = shader.flags;
	if (shader.transparent) {
		data.flags |= YutrelMaterialData::TRANSPARENT;
	}
	data.blend_mode = shader.blend_mode;
	const auto param = [&](const StringName &p_name) { return materials->material_get_param(p_material, p_name); };
	const auto scalar = [&](const StringName &p_name, float p_default) {
		const Variant value = param(p_name);
		return value.is_num() ? float(value) : p_default;
	};
	const auto texture = [&](TextureSlot p_slot, const StringName &p_name, bool p_srgb = false) {
		const RID rd_texture = textures->texture_get_rd_texture(param(p_name), p_srgb);
		if (rd_texture.is_valid()) {
			r_material.textures[p_slot] = rd_texture;
		}
		return rd_texture.is_valid();
	};

	const Variant albedo = param(SNAME("albedo"));
	if (albedo.get_type() == Variant::COLOR) {
		const Color linear = Color(albedo).srgb_to_linear();
		for (int i = 0; i < 4; ++i) {
			data.albedo[i] = linear[i];
		}
	}
	texture(ALBEDO, SNAME("texture_albedo"), true);
	if (shader.normal_enabled && texture(NORMAL, SNAME("texture_normal"))) {
		data.flags |= YutrelMaterialData::HAS_NORMAL_MAP;
	}
	data.normal_scale = scalar(SNAME("normal_scale"), 1.0f);
	data.specular = scalar(SNAME("specular"), 0.5f);
	data.alpha_scissor_threshold = scalar(SNAME("alpha_scissor_threshold"), 0.5f);
	if (shader.orm) {
		// ORM's channels replace the separate metallic and roughness parameters.
		if (texture(ROUGHNESS, SNAME("texture_orm"))) {
			r_material.textures[METALLIC] = r_material.textures[ROUGHNESS];
			if (shader.ao_enabled) {
				r_material.textures[AO] = r_material.textures[ROUGHNESS];
			}
			data.metallic = 1;
			store_mask(data.roughness_channel, Vector4(0, 1, 0, 0));
			store_mask(data.metallic_channel, Vector4(0, 0, 1, 0));
		}
	} else {
		data.roughness = scalar(SNAME("roughness"), 1.0f);
		data.metallic = scalar(SNAME("metallic"), 0.0f);
		texture(ROUGHNESS, SNAME("texture_roughness"));
		texture(METALLIC, SNAME("texture_metallic"));
		store_mask(data.roughness_channel, shader.roughness_channel);
		const Variant metallic_channel = param(SNAME("metallic_texture_channel"));
		if (metallic_channel.get_type() == Variant::VECTOR4) {
			store_mask(data.metallic_channel, metallic_channel);
		}
		if (shader.ao_enabled && texture(AO, SNAME("texture_ambient_occlusion"))) {
			const Variant ao_channel = param(SNAME("ao_texture_channel"));
			if (ao_channel.get_type() == Variant::VECTOR4) {
				store_mask(data.ao_channel, ao_channel);
			}
		}
	}

	if (shader.emission_enabled) {
		const Variant emission = param(SNAME("emission"));
		if (emission.get_type() == Variant::COLOR) {
			const Color linear = Color(emission).srgb_to_linear();
			for (int i = 0; i < 3; ++i) {
				data.emission[i] = linear[i];
			}
		}
		data.emission_energy = scalar(SNAME("emission_energy"), 1.0f);
		if (texture(EMISSION, SNAME("texture_emission"), true)) {
			data.flags |= YutrelMaterialData::HAS_EMISSION_TEXTURE;
		}
	}
	const Variant uv_scale = param(SNAME("uv1_scale"));
	const Variant uv_offset = param(SNAME("uv1_offset"));
	if (uv_scale.get_type() == Variant::VECTOR3) {
		const Vector3 uv = uv_scale;
		data.uv_transform[0] = uv.x;
		data.uv_transform[1] = uv.y;
	}
	if (uv_offset.get_type() == Variant::VECTOR3) {
		const Vector3 uv = uv_offset;
		data.uv_transform[2] = uv.x;
		data.uv_transform[3] = uv.y;
	}
	return true;
}

RID YutrelMaterialBridge::get_uniform_set(RID p_shader, const Material &p_material) const {
	const RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
	return UniformSetCacheRD::get_singleton()->get_cache(p_shader, 1,
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, ALBEDO, p_material.textures[ALBEDO]),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, NORMAL, p_material.textures[NORMAL]),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, ROUGHNESS, p_material.textures[ROUGHNESS]),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, METALLIC, p_material.textures[METALLIC]),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, AO, p_material.textures[AO]),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, EMISSION, p_material.textures[EMISSION]),
			RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, TEXTURE_COUNT, sampler));
}

} // namespace RendererSceneRenderImplementation
