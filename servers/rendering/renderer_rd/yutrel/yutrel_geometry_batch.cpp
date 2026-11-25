/**************************************************************************/
/*  yutrel_geometry_batch.cpp                                             */
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

#include "yutrel_geometry_batch.h"

#ifdef TOOLS_ENABLED
#include "yutrel_rt_scene.h"
#endif

#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

bool YutrelGeometryBatch::prepare() {
	static constexpr const char *shaders[] = { "base_pass.slang", "shadow_caster.slang", "transparent.slang" };
	return prepare_shader(shader, pipelines, shaders[pass]);
}

bool YutrelGeometryBatch::prepare_shader(YutrelShader &r_shader, PipelineTable &r_pipelines, const char *p_path) {
	if (!refresh_shader(r_shader, p_path)) {
		return r_shader.get_shader().is_valid();
	}
	static constexpr int attachment_count[] = { 5, 0, 2 };
	RD::PipelineColorBlendState blend = RD::PipelineColorBlendState::create_disabled(attachment_count[pass]);
	RD::PipelineDepthStencilState depth;
	depth.enable_depth_test = true;
	depth.enable_depth_write = pass == SHADOW;
	depth.depth_compare_operator = pass == BASE ? RD::COMPARE_OP_EQUAL : RD::COMPARE_OP_GREATER_OR_EQUAL;
	using ShaderData = RendererRD::MaterialStorage::ShaderData;
	static constexpr ShaderData::BlendMode modes[YutrelMaterialBridge::BLEND_MODE_COUNT] = {
		ShaderData::BLEND_MODE_MIX, ShaderData::BLEND_MODE_ADD, ShaderData::BLEND_MODE_SUB, ShaderData::BLEND_MODE_MUL
	};
	const int mode_count = pass == TRANSPARENT ? YutrelMaterialBridge::BLEND_MODE_COUNT : 1;
	for (int mode = 0; mode < mode_count; mode++) {
		if (pass == TRANSPARENT) {
			auto attachment = ShaderData::blend_mode_to_blend_attachment(modes[mode]);
			attachment.write_a = false;
			blend.attachments.write[0] = attachment;
			auto &reactive = blend.attachments.write[1];
			reactive.enable_blend = true;
			reactive.color_blend_op = reactive.alpha_blend_op = RD::BLEND_OP_MAXIMUM;
			reactive.src_color_blend_factor = reactive.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
			reactive.src_alpha_blend_factor = reactive.dst_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
		}
		setup_cull_pipelines(r_pipelines[mode], r_shader.get_shader(), depth, blend);
	}
	return r_shader.get_shader().is_valid();
}

void YutrelGeometryBatch::setup_cull_pipelines(CullPipelines &r_pipelines, RID p_shader, const RD::PipelineDepthStencilState &p_depth, const RD::PipelineColorBlendState &p_blend) {
	for (int mirrored = 0; mirrored < 2; mirrored++) {
		for (int double_sided = 0; double_sided < 2; double_sided++) {
			RD::PipelineRasterizationState raster;
			// Godot's projection flips Y for Vulkan's positive-height viewport.
			raster.front_face = mirrored ? RD::POLYGON_FRONT_FACE_COUNTER_CLOCKWISE : RD::POLYGON_FRONT_FACE_CLOCKWISE;
			raster.cull_mode = double_sided ? RD::POLYGON_CULL_DISABLED : RD::POLYGON_CULL_BACK;
			r_pipelines[mirrored][double_sided].setup(p_shader, RD::RENDER_PRIMITIVE_TRIANGLES, raster, {}, p_depth, p_blend);
		}
	}
}

void YutrelGeometryBatch::clear() {
	draws.clear();
	instances.clear();
	materials.clear();
	material_sets.clear();
	material_albedos.clear();
	material_indices.clear();
}

YutrelGeometryBatch::BatchMaterial YutrelGeometryBatch::resolve_material(RID p_material, YutrelMaterialBridge &p_bridge, YutrelGeometryBatch *p_transparent) {
	const BatchMaterial *cached = material_indices.getptr(p_material);
	if (cached) {
		return *cached;
	}
	BatchMaterial entry;
	YutrelMaterialBridge::Material material;
	if (p_bridge.get_material(p_material, material) && (!material.transparent || p_transparent)) {
		YutrelGeometryBatch &batch = material.transparent ? *p_transparent : *this;
		entry.id = batch.materials.size();
		entry.transparent = material.transparent;
		entry.alpha_scissor = (material.data.flags & YutrelMaterialData::ALPHA_SCISSOR) != 0;
		entry.priority = material.render_priority;
		entry.blend = material.blend_mode;
		batch.materials.push_back(material.data);
		if (pass == SHADOW) {
			// Opaque specialization never samples; all opaque materials share one descriptor.
			const RID albedo = entry.alpha_scissor ? material.textures[YutrelMaterialBridge::ALBEDO] : p_bridge.get_white_texture();
			const RID sampler = p_bridge.get_material_sampler();
			batch.material_sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 1,
					RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, albedo), RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 1, sampler)));
		} else {
			batch.material_sets.push_back(p_bridge.get_uniform_set(batch.shader.get_shader(), material));
			if (batch.pass == BASE) {
				batch.material_albedos.push_back(entry.alpha_scissor ? material.textures[YutrelMaterialBridge::ALBEDO] : p_bridge.get_white_texture());
			}
		}
	}
	material_indices.insert(p_material, entry);
	return entry;
}

void YutrelGeometryBatch::add_geometry(const RenderGeometryInstanceBase &p_instance, bool p_camera_mirrored, YutrelMaterialBridge &p_material_bridge, YutrelGeometryBatch *p_transparent, const Vector3 &p_camera_position, const Vector3 &p_camera_forward, const Transform3D *p_prev_transform) {
	const auto &data = *p_instance.data;
	auto *meshes = RendererRD::MeshStorage::get_singleton();
	if (data.base_type != RSE::INSTANCE_MESH || data.skeleton.is_valid() || meshes->mesh_get_blend_shape_count(data.base) != 0) {
		return;
	}
	const real_t determinant = p_instance.transform.basis.determinant();
	if (!Math::is_finite(determinant) || determinant == 0) {
		return;
	}

	YutrelInstanceData gpu_instance;
	gpu_instance.set_transform(p_instance.transform, p_instance.transform.basis.inverse().transposed());
	if (p_prev_transform) {
		RendererRD::MaterialStorage::store_transform_transposed_3x4(*p_prev_transform, &gpu_instance.prev_model[0][0]);
	}
	const bool mirrored = (determinant < 0) != p_camera_mirrored;
	const int surface_count = meshes->mesh_get_surface_count(data.base);
	for (int surface_index = 0; surface_index < surface_count; surface_index++) {
		void *surface = meshes->mesh_get_surface(data.base, uint32_t(surface_index));
		const uint64_t format = meshes->mesh_surface_get_format(surface);
		constexpr uint64_t required = RSE::ARRAY_FORMAT_VERTEX | RSE::ARRAY_FORMAT_NORMAL;
		constexpr uint64_t unsupported = RSE::ARRAY_FLAG_USE_2D_VERTICES | RSE::ARRAY_FORMAT_BONES | RSE::ARRAY_FORMAT_WEIGHTS;
		const uint32_t vertices_drawn = meshes->mesh_surface_get_vertices_drawn_count(surface);
		if ((format & required) != required || (format & unsupported) ||
				meshes->mesh_surface_get_primitive(surface) != RSE::PRIMITIVE_TRIANGLES ||
				vertices_drawn == 0) {
			continue;
		}

		const RID material_rid = YutrelMaterialBridge::get_surface_material(p_instance, data.base, surface_index);
		const BatchMaterial entry = resolve_material(material_rid, p_material_bridge, p_transparent);
		if (entry.id < 0) {
			continue;
		}
		YutrelGeometryBatch &batch = entry.transparent ? *p_transparent : *this;
		const YutrelMaterialData &material = batch.materials[entry.id];

		YutrelInstanceData surface_instance = gpu_instance;
		surface_instance.material_id = entry.id;
		if (format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
			surface_instance.set_compressed_aabb(meshes->mesh_surface_get_aabb(surface));
			const Vector4 uv_scale = meshes->mesh_surface_get_uv_scale(surface);
			surface_instance.uv_scale[0] = uv_scale.x;
			surface_instance.uv_scale[1] = uv_scale.y;
		}
		if (format & RSE::ARRAY_FORMAT_TANGENT) {
			surface_instance.flags |= YutrelInstanceData::HAS_TANGENT;
		}
		if (format & RSE::ARRAY_FORMAT_COLOR) {
			surface_instance.flags |= YutrelInstanceData::HAS_COLOR;
		}

		DrawItem item;
		item.surface = surface;
		item.instance_id = batch.instances.size();
		item.primitive_count = vertices_drawn / 3;
		item.material = entry;
		if (pass != SHADOW) {
			const Vector3 world_center = p_instance.transform.xform(meshes->mesh_surface_get_aabb(surface).get_center());
			item.distance = (p_camera_position - world_center).dot(p_camera_forward);
		}
		item.mirrored = mirrored != ((material.flags & YutrelMaterialData::CULL_FRONT) != 0);
		item.double_sided = (pass == SHADOW && data.cast_double_sided_shadows) || (material.flags & YutrelMaterialData::DOUBLE_SIDED) != 0;
		batch.draws.push_back(item);
		batch.instances.push_back(surface_instance);
	}
}

#ifdef TOOLS_ENABLED
uint64_t YutrelGeometryBatch::get_render_hash(uint64_t p_hash) const {
	p_hash = rt_hash_bytes(p_hash, instances.ptr(), instances.size() * sizeof(YutrelInstanceData));
	p_hash = rt_hash_bytes(p_hash, materials.ptr(), materials.size() * sizeof(YutrelMaterialData));
	for (const DrawItem &item : draws) {
		p_hash = hash_djb2_one_64(uintptr_t(item.surface), p_hash);
		p_hash = hash_djb2_one_64(material_sets[item.material.id].get_id(), p_hash);
	}
	return p_hash;
}
#endif

bool YutrelGeometryBatch::upload_geometry() {
	return instance_buffer.upload(instances, "Yutrel Instance SSBO") && material_buffer.upload(materials, "Yutrel Material SSBO");
}

void YutrelGeometryBatch::resolve_draw(DrawItem &r_item, RD::FramebufferFormatID p_format, PipelineCacheRD &p_pipeline, bool p_specialize) {
	auto *meshes = RendererRD::MeshStorage::get_singleton();
	RD::VertexFormatID vertex_format;
	meshes->mesh_surface_get_vertex_arrays_and_format(r_item.surface, p_pipeline.get_vertex_input_mask(), false, false, r_item.vertex_array, vertex_format);
	r_item.index_array = meshes->mesh_surface_get_index_array(r_item.surface, 0);
	r_item.pipeline = p_pipeline.get_render_pipeline(vertex_format, p_format, false, 0, p_specialize);
}

void YutrelGeometryBatch::resolve_draws(RD::FramebufferFormatID p_format, PipelineTable &p_pipelines) {
	for (DrawItem &item : draws) {
		const auto mode = pass == TRANSPARENT ? item.material.blend : YutrelMaterialBridge::BLEND_MIX;
		const bool specialize = pass == TRANSPARENT ? mode == YutrelMaterialBridge::BLEND_MUL : item.material.alpha_scissor;
		resolve_draw(item, p_format, p_pipelines[mode][item.mirrored][item.double_sided], specialize);
	}
}

bool YutrelGeometryBatch::bind_draw(RD::DrawListID p_list, const DrawItem &p_item, RID p_material_set, RID &r_bound_pipeline, RID &r_bound_material_set) {
	if (p_item.pipeline.is_null() || p_item.vertex_array.is_null() || p_material_set.is_null()) {
		return false;
	}
	RD *rd = RD::get_singleton();
	if (p_item.pipeline != r_bound_pipeline) {
		rd->draw_list_bind_render_pipeline(p_list, p_item.pipeline);
		r_bound_pipeline = p_item.pipeline;
	}
	if (p_material_set != r_bound_material_set && (pass != SHADOW || r_bound_material_set.is_null() || p_item.material.alpha_scissor)) {
		rd->draw_list_bind_uniform_set(p_list, p_material_set, 1);
		r_bound_material_set = p_material_set;
	}
	rd->draw_list_bind_vertex_array(p_list, p_item.vertex_array);
	if (p_item.index_array.is_valid()) {
		rd->draw_list_bind_index_array(p_list, p_item.index_array);
	}
	return true;
}

} // namespace RendererSceneRenderImplementation
