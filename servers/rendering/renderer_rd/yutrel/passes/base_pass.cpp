/**************************************************************************/
/*  base_pass.cpp                                                         */
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

#include "base_pass.h"

#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

bool BasePass::prepare() {
	if (refresh_shader(base_shader, "base_pass.slang")) {
		const RD::PipelineColorBlendState blend = RD::PipelineColorBlendState::create_disabled(4);
		RD::PipelineDepthStencilState depth;
		depth.enable_depth_test = true;
		depth.enable_depth_write = true;
		depth.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;
		for (int mirrored = 0; mirrored < 2; mirrored++) {
			for (int double_sided = 0; double_sided < 2; double_sided++) {
				RD::PipelineRasterizationState raster;
				// Godot's projection flips Y for Vulkan's positive-height viewport.
				raster.front_face = mirrored ? RD::POLYGON_FRONT_FACE_COUNTER_CLOCKWISE : RD::POLYGON_FRONT_FACE_CLOCKWISE;
				raster.cull_mode = double_sided ? RD::POLYGON_CULL_DISABLED : RD::POLYGON_CULL_BACK;
				base_pipelines[mirrored][double_sided].setup(base_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, raster, {}, depth, blend);
			}
		}
	}
	return base_shader.get_shader().is_valid();
}

void BasePass::clear() {
	draws.clear();
	instances.clear();
	materials.clear();
	material_sets.clear();
	material_indices.clear();
}

void BasePass::add(const RenderGeometryInstanceBase &p_instance, bool p_camera_mirrored, YutrelMaterialBridge &p_material_bridge) {
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
	const bool mirrored = (determinant < 0) != p_camera_mirrored;
	const int surface_count = meshes->mesh_get_surface_count(data.base);
	for (int surface_index = 0; surface_index < surface_count; surface_index++) {
		void *surface = meshes->mesh_get_surface(data.base, uint32_t(surface_index));
		const uint64_t format = meshes->mesh_surface_get_format(surface);
		constexpr uint64_t required = RSE::ARRAY_FORMAT_VERTEX | RSE::ARRAY_FORMAT_NORMAL;
		constexpr uint64_t unsupported = RSE::ARRAY_FLAG_USE_2D_VERTICES | RSE::ARRAY_FORMAT_BONES | RSE::ARRAY_FORMAT_WEIGHTS;
		if ((format & required) != required || (format & unsupported) ||
				meshes->mesh_surface_get_primitive(surface) != RSE::PRIMITIVE_TRIANGLES ||
				meshes->mesh_surface_get_vertices_drawn_count(surface) == 0) {
			continue;
		}

		const RID material_rid = YutrelMaterialBridge::get_surface_material(p_instance, data.base, surface_index);
		const int *cached_index = material_indices.getptr(material_rid);
		int material_id = cached_index ? *cached_index : -1;
		if (!cached_index) {
			YutrelMaterialBridge::Material material;
			if (p_material_bridge.get_material(material_rid, material)) {
				material_id = materials.size();
				materials.push_back(material.data);
				material_sets.push_back(p_material_bridge.get_uniform_set(base_shader.get_shader(), material));
			}
			material_indices.insert(material_rid, material_id);
		}
		if (material_id < 0) {
			continue;
		}
		const YutrelMaterialData &material = materials[material_id];

		YutrelInstanceData surface_instance = gpu_instance;
		surface_instance.material_id = material_id;
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
		item.mirrored = mirrored != ((material.flags & YutrelMaterialData::CULL_FRONT) != 0);
		item.double_sided = (material.flags & YutrelMaterialData::DOUBLE_SIDED) != 0;
		draws.push_back(item);
		instances.push_back(surface_instance);
	}
}

bool BasePass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const Color &p_clear_color) {
	RID base_set;
	if (has_draws()) {
		ERR_FAIL_COND_V(!instance_buffer.upload(instances, "Yutrel Instance SSBO"), false);
		ERR_FAIL_COND_V(!material_buffer.upload(materials, "Yutrel Material SSBO"), false);
		base_set = UniformSetCacheRD::get_singleton()->get_cache(base_shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, material_buffer.get_buffer()));
		ERR_FAIL_COND_V(base_set.is_null(), false);
	}
	_record(p_targets, base_set, p_clear_color);
	return true;
}

void BasePass::_record(const YutrelPipelineTargets &p_targets, RID p_uniform_set, const Color &p_clear_color) {
	RD *rd = RD::get_singleton();
	auto *meshes = RendererRD::MeshStorage::get_singleton();
	const auto framebuffer_format = rd->framebuffer_get_format(p_targets.base_framebuffer);
	for (DrawItem &item : draws) {
		auto &pipeline = base_pipelines[item.mirrored][item.double_sided];
		RD::VertexFormatID vertex_format;
		meshes->mesh_surface_get_vertex_arrays_and_format(item.surface, pipeline.get_vertex_input_mask(), false, false, item.vertex_array, vertex_format);
		item.index_array = meshes->mesh_surface_get_index_array(item.surface, 0);
		item.pipeline = pipeline.get_render_pipeline(vertex_format, framebuffer_format);
	}

	rd->draw_command_begin_label("Yutrel BasePass", Color(0.2, 0.7, 1.0));
	const Color zero(0, 0, 0, 0);
	const RD::DrawListID list = rd->draw_list_begin(p_targets.base_framebuffer, RD::DRAW_CLEAR_COLOR_ALL | RD::DRAW_CLEAR_DEPTH, Vector<Color>{ p_clear_color, zero, zero, zero }, 0.0f, 0, p_targets.region, RDD::BreadcrumbMarker::OPAQUE_PASS);
	if (p_uniform_set.is_valid()) {
		rd->draw_list_bind_uniform_set(list, p_uniform_set, 0);
	}
	RID bound_material_set;
	for (uint32_t draw_id = 0; draw_id < draws.size(); draw_id++) {
		const DrawItem &item = draws[draw_id];
		const RID material_set = material_sets[instances[draw_id].material_id];
		if (item.pipeline.is_null() || item.vertex_array.is_null() || material_set.is_null()) {
			continue;
		}
		rd->draw_list_bind_render_pipeline(list, item.pipeline);
		if (material_set != bound_material_set) {
			rd->draw_list_bind_uniform_set(list, material_set, 1);
			bound_material_set = material_set;
		}
		rd->draw_list_bind_vertex_array(list, item.vertex_array);
		if (item.index_array.is_valid()) {
			rd->draw_list_bind_index_array(list, item.index_array);
		}
		rd->draw_list_set_push_constant(list, &draw_id, sizeof(draw_id));
		rd->draw_list_draw(list, item.index_array.is_valid());
	}
	rd->draw_list_end();
	rd->draw_command_end_label();
}

} // namespace RendererSceneRenderImplementation
