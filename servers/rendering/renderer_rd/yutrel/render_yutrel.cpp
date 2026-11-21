/**************************************************************************/
/*  render_yutrel.cpp                                                     */
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

#include "render_yutrel.h"

#include "core/config/engine.h"
#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/utilities.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

YutrelFrameData RenderYutrel::_build_frame_data(const RenderDataRD &p_data) const {
	YutrelFrameData frame;
	const Projection vp = p_data.scene_data->get_cam_projection() * Projection(p_data.scene_data->cam_transform.affine_inverse());
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			frame.view_projection[row][column] = vp.columns[column][row];
		}
	}

	RendererRD::LightStorage *lights = RendererRD::LightStorage::get_singleton();
	if (p_data.lights) {
		for (uint32_t i = 0; i < p_data.lights->size(); i++) {
			const RID instance = (*p_data.lights)[i];
			const RID light = lights->light_instance_get_base_light(instance);
			if (lights->light_get_type(light) != RSE::LIGHT_DIRECTIONAL || lights->light_directional_get_sky_mode(light) == RSE::LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY) {
				continue;
			}
			// Directional light travels along local -Z. Lambert uses the direction towards the light.
			const Vector3 direction = lights->light_instance_get_base_transform(instance).basis.get_column(2).normalized();
			const float energy = lights->light_get_param(light, RSE::LIGHT_PARAM_ENERGY) * (lights->light_is_negative(light) ? -1.0f : 1.0f);
			const Color radiance = lights->light_get_color(light).srgb_to_linear() * energy;
			for (int axis = 0; axis < 3; axis++) {
				frame.light_direction[axis] = direction[axis];
				frame.light_radiance[axis] = radiance[axis];
			}
			frame.light_cull_mask = lights->light_get_cull_mask(light);
			break;
		}
	}
	return frame;
}

void RenderYutrel::_build_draw_list(const RenderDataRD &p_data) {
	draws.clear();
	instances.clear();
#ifdef TOOLS_ENABLED
	editor_gizmos.clear();
	const bool editor_view = Engine::get_singleton()->is_editor_hint();
	constexpr uint32_t scene_layer_mask = (1u << 20) - 1;
#endif
	if (!p_data.instances) {
		return;
	}

	RendererRD::MeshStorage *meshes = RendererRD::MeshStorage::get_singleton();
	for (uint32_t i = 0; i < p_data.instances->size(); i++) {
		const auto &instance = *static_cast<const GeometryInstanceYutrel *>((*p_data.instances)[i]);
		const auto &data = *instance.data;
#ifdef TOOLS_ENABLED
		if (editor_view && !(instance.layer_mask & scene_layer_mask)) {
			editor_gizmos.add(instance, p_data.scene_data->cam_transform);
			continue;
		}
#endif
		if (data.base_type != RSE::INSTANCE_MESH || data.skeleton.is_valid() || meshes->mesh_get_blend_shape_count(data.base) != 0) {
			continue;
		}

		const real_t determinant = instance.transform.basis.determinant();
		if (!Math::is_finite(determinant) || determinant == 0) {
			continue;
		}

		YutrelInstanceData gpu_instance;
		gpu_instance.set_transform(instance.transform, instance.transform.basis.inverse().transposed());
		gpu_instance.layer_mask = instance.layer_mask;
		const bool mirrored = (determinant < 0) != (p_data.scene_data->cam_transform.basis.determinant() < 0);
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
			YutrelInstanceData surface_instance = gpu_instance;
			if (format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
				surface_instance.set_compressed_aabb(meshes->mesh_surface_get_aabb(surface));
			}
			DrawItem item;
			item.surface = surface;
			item.mirrored = mirrored;
			draws.push_back(item);
			instances.push_back(surface_instance);
		}
	}
}

bool RenderYutrel::_get_native_targets(const RenderDataRD &p_data, YutrelPipelineTargets &r_targets) const {
	const Ref<RenderSceneBuffersRD> &buffers = p_data.render_buffers;
	auto *textures = RendererRD::TextureStorage::get_singleton();
	r_targets.render_target = buffers->get_render_target();
	ERR_FAIL_COND_V(r_targets.render_target.is_null(), false);
	const Size2i size = buffers->get_target_size();
	if (size.x <= 0 || size.y <= 0) {
		return false;
	}
	if (buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED || textures->render_target_get_msaa(r_targets.render_target) != RSE::VIEWPORT_MSAA_DISABLED) {
		WARN_PRINT_ONCE("Yutrel requires MSAA to be disabled.");
		return false;
	}
	if (buffers->get_view_count() != 1 || p_data.scene_data->get_view_count() != 1 || buffers->get_use_taa()) {
		WARN_PRINT_ONCE("Yutrel requires a single view with TAA disabled.");
		return false;
	}
	if (buffers->get_internal_size() != size || textures->render_target_get_size(r_targets.render_target) != size) {
		WARN_PRINT_ONCE("Yutrel requires equal internal and output sizes.");
		return false;
	}

	const Rect2i full_region(Point2i(), size);
	r_targets.region = p_data.render_region == Rect2i() ? full_region : p_data.render_region.intersection(full_region);
	if (!r_targets.region.has_area()) {
		return false;
	}
	r_targets.scene_color = buffers->get_internal_texture();
	r_targets.scene_depth = buffers->get_depth_texture();
	r_targets.output_color = textures->render_target_get_rd_texture(r_targets.render_target);
	ERR_FAIL_COND_V(r_targets.scene_color.is_null() || r_targets.scene_depth.is_null() || r_targets.output_color.is_null(), false);
	ERR_FAIL_COND_V(RD::get_singleton()->texture_get_format(r_targets.scene_color).format != RD::DATA_FORMAT_R16G16B16A16_SFLOAT, false);
	auto *framebuffers = FramebufferCacheRD::get_singleton();
	r_targets.forward_framebuffer = framebuffers->get_cache(r_targets.scene_color, r_targets.scene_depth);
	r_targets.output_framebuffer = framebuffers->get_cache(r_targets.output_color);
	r_targets.output_linear = textures->render_target_is_using_hdr(r_targets.render_target);
	return r_targets.forward_framebuffer.is_valid() && r_targets.output_framebuffer.is_valid();
}

bool RenderYutrel::_prepare_passes() {
	const uint64_t forward_version = forward_shader.get_version();
	const uint64_t output_version = output_shader.get_version();
	if (!forward_shader.is_loaded()) {
		forward_shader.load("forward.slang");
		output_shader.load("output.slang");
	}
	forward_shader.poll_hot_reload();
	output_shader.poll_hot_reload();

	// setup() clears old variants, including RIDs already freed by the shader cascade.
	if (forward_shader.get_version() != forward_version) {
		RD::PipelineRasterizationState raster;
		raster.cull_mode = RD::POLYGON_CULL_BACK;
		// Godot's camera projection flips Y for Vulkan's positive-height viewport.
		raster.front_face = RD::POLYGON_FRONT_FACE_CLOCKWISE;
		RD::PipelineDepthStencilState depth;
		depth.enable_depth_test = true;
		depth.enable_depth_write = true;
		depth.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;
		forward_pipelines[0].setup(forward_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, raster, {}, depth, RD::PipelineColorBlendState::create_disabled());
		raster.front_face = RD::POLYGON_FRONT_FACE_COUNTER_CLOCKWISE;
		forward_pipelines[1].setup(forward_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, raster, {}, depth, RD::PipelineColorBlendState::create_disabled());
	}
	if (output_shader.get_version() != output_version) {
		output_pipeline.setup(output_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	return forward_shader.get_shader().is_valid() && output_shader.get_shader().is_valid();
}

void RenderYutrel::_record_forward_pass(const YutrelPipelineTargets &p_targets, RID p_uniform_set, const Color &p_clear_color) {
	RD *rd = RD::get_singleton();
	auto *meshes = RendererRD::MeshStorage::get_singleton();
	const RD::FramebufferFormatID framebuffer_format = rd->framebuffer_get_format(p_targets.forward_framebuffer);
	for (DrawItem &item : draws) {
		auto &pipeline = forward_pipelines[item.mirrored];
		RD::VertexFormatID vertex_format;
		meshes->mesh_surface_get_vertex_arrays_and_format(item.surface, pipeline.get_vertex_input_mask(), false, false, item.vertex_array, vertex_format);
		item.index_array = meshes->mesh_surface_get_index_array(item.surface, 0);
		item.pipeline = pipeline.get_render_pipeline(vertex_format, framebuffer_format);
	}

	rd->draw_command_begin_label("Yutrel ForwardPass", Color(0.2, 0.7, 1.0));
	const RD::DrawListID list = rd->draw_list_begin(p_targets.forward_framebuffer, RD::DRAW_CLEAR_COLOR_0 | RD::DRAW_CLEAR_DEPTH, Vector<Color>{ p_clear_color }, 0.0f, 0, p_targets.region, RDD::BreadcrumbMarker::OPAQUE_PASS);
	if (p_uniform_set.is_valid()) {
		rd->draw_list_bind_uniform_set(list, p_uniform_set, 0);
	}
	for (uint32_t draw_id = 0; draw_id < draws.size(); draw_id++) {
		const DrawItem &item = draws[draw_id];
		if (item.pipeline.is_null() || item.vertex_array.is_null()) {
			continue;
		}
		rd->draw_list_bind_render_pipeline(list, item.pipeline);
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

void RenderYutrel::_record_output_pass(const YutrelPipelineTargets &p_targets, RID p_pipeline, RID p_uniform_set) {
	RD *rd = RD::get_singleton();
	const uint32_t output_linear = p_targets.output_linear;
	rd->draw_command_begin_label("Yutrel OutputPass", Color(0.7, 0.9, 0.2));
	const RD::DrawListID list = rd->draw_list_begin(p_targets.output_framebuffer, RD::DRAW_DEFAULT_ALL, {}, 0.0f, 0, p_targets.region);
	rd->draw_list_bind_render_pipeline(list, p_pipeline);
	rd->draw_list_bind_uniform_set(list, p_uniform_set, 0);
	rd->draw_list_set_push_constant(list, &output_linear, sizeof(output_linear));
	rd->draw_list_draw(list, false, 1, 3);
	rd->draw_list_end();
	rd->draw_command_end_label();
}

void RenderYutrel::_render_scene(RenderDataRD *p_render_data, const Color &p_default_color) {
	ERR_FAIL_NULL(p_render_data);
	if (p_render_data->reflection_probe.is_valid()) {
		return;
	}
	ERR_FAIL_COND(p_render_data->render_buffers.is_null());
	ERR_FAIL_NULL(p_render_data->scene_data);
	YutrelPipelineTargets targets;
	if (!_get_native_targets(*p_render_data, targets) || !_prepare_passes()) {
		return;
	}

	RD *rd = RD::get_singleton();
	auto *uniform_sets = UniformSetCacheRD::get_singleton();
	const RID output_set = uniform_sets->get_cache(output_shader.get_shader(), 0, RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, targets.scene_color));
	const RID output = output_pipeline.get_render_pipeline(RD::INVALID_FORMAT_ID, rd->framebuffer_get_format(targets.output_framebuffer));
	ERR_FAIL_COND(output_set.is_null() || output.is_null());

	const YutrelFrameData frame = _build_frame_data(*p_render_data);
	_build_draw_list(*p_render_data);
	RID forward_set;
	if (!draws.is_empty()) {
		if (frame_buffer.is_null()) {
			frame_buffer = rd->uniform_buffer_create(sizeof(frame));
			ERR_FAIL_COND(frame_buffer.is_null());
			rd->set_resource_name(frame_buffer, "Yutrel Frame UBO");
		}
		ERR_FAIL_COND(rd->buffer_update(frame_buffer, 0, sizeof(frame), &frame) != OK);
		ERR_FAIL_COND(!instance_buffer.upload(instances, "Yutrel Instance SSBO"));
		forward_set = uniform_sets->get_cache(forward_shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, frame_buffer),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()));
		ERR_FAIL_COND(forward_set.is_null());
	}

	_record_forward_pass(targets, forward_set, p_default_color.srgb_to_linear());
	_record_output_pass(targets, output, output_set);
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		editor_gizmos.render(targets, *p_render_data->scene_data, frame);
	}
#endif
	// Canvas composites over the completed scene.
	RendererRD::TextureStorage::get_singleton()->render_target_disable_clear_request(targets.render_target);
}

RenderGeometryInstance *RenderYutrel::geometry_instance_create(RID p_base) {
	const RSE::InstanceType type = RendererRD::Utilities::get_singleton()->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RSE::INSTANCE_GEOMETRY_MASK), nullptr);
	return memnew(GeometryInstanceYutrel(p_base, type));
}

void RenderYutrel::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	ERR_FAIL_NULL(p_geometry_instance);
	memdelete(static_cast<GeometryInstanceYutrel *>(p_geometry_instance));
}

RenderYutrel::RenderYutrel() {
	sky.set_texture_format(_render_buffers_get_preferred_color_format());
	print_verbose("Yutrel: Slang Forward and Output renderer created.");
}

RenderYutrel::~RenderYutrel() {
	if (frame_buffer.is_valid()) {
		RD::get_singleton()->free_rid(frame_buffer);
	}
	// This can call base_uniforms_changed(), so release before the base destructor.
	RendererRD::LightStorage::get_singleton()->directional_shadow_atlas_set_size(0);
}

} // namespace RendererSceneRenderImplementation
