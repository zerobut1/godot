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

#include "yutrel_environment_settings.h"
#include "yutrel_viewport_settings.h"

#include "core/config/engine.h"
#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/utilities.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

namespace {

class YutrelRenderBufferData : public RenderBufferCustomDataRD {
	GDCLASS(YutrelRenderBufferData, RenderBufferCustomDataRD);

public:
	RID gbuffer_a;
	RID gbuffer_b;
	RID gbuffer_c;

	void configure(RenderSceneBuffersRD *p_buffers) override {
		if (p_buffers->get_view_count() != 1 || p_buffers->get_internal_size().x <= 0 || p_buffers->get_internal_size().y <= 0) {
			return;
		}
		constexpr uint32_t usage = RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
		gbuffer_a = p_buffers->create_texture(SNAME("Yutrel"), SNAME("GBuffer_A"), RD::DATA_FORMAT_R8G8B8A8_SRGB, usage);
		gbuffer_b = p_buffers->create_texture(SNAME("Yutrel"), SNAME("GBuffer_B"), RD::DATA_FORMAT_R16G16B16A16_UNORM, usage);
		gbuffer_c = p_buffers->create_texture(SNAME("Yutrel"), SNAME("GBuffer_C"), RD::DATA_FORMAT_R8G8B8A8_UNORM, usage);
	}

	void free_data() override {
		// RenderSceneBuffersRD owns the textures created through its named storage.
		gbuffer_a = RID();
		gbuffer_b = RID();
		gbuffer_c = RID();
	}
};

bool refresh_shader(YutrelShader &p_shader, const char *p_path) {
	const uint64_t version = p_shader.get_version();
	if (!p_shader.is_loaded()) {
		p_shader.load(p_path);
	}
	p_shader.poll_hot_reload();
	return p_shader.get_version() != version;
}

struct DebugViewPushConstant {
	uint32_t output_linear;
	uint32_t mode;
};

struct OutputPushConstant {
	uint32_t output_linear;
	uint32_t tone_mapping;
	float exposure;
};

static_assert(sizeof(DebugViewPushConstant) == 8);
static_assert(sizeof(OutputPushConstant) == 12);
static_assert(offsetof(OutputPushConstant, exposure) == 8);

void draw_fullscreen(Span<char> p_label, const Color &p_color, const Rect2i &p_region, RID p_framebuffer, PipelineCacheRD &p_pipeline, RID p_uniform_set, const void *p_constants = nullptr, uint32_t p_constants_size = 0, bool p_clear = false) {
	RD *rd = RD::get_singleton();
	const RID pipeline = p_pipeline.get_render_pipeline(RD::INVALID_FORMAT_ID, rd->framebuffer_get_format(p_framebuffer));
	ERR_FAIL_COND(pipeline.is_null() || p_uniform_set.is_null());

	rd->draw_command_begin_label(p_label, p_color);
	const Vector<Color> clear_colors = p_clear ? Vector<Color>{ Color(0, 0, 0, 0) } : Vector<Color>();
	const RD::DrawListID list = rd->draw_list_begin(p_framebuffer, p_clear ? RD::DRAW_CLEAR_COLOR_0 : RD::DRAW_DEFAULT_ALL, clear_colors, 0.0f, 0, p_region);
	rd->draw_list_bind_render_pipeline(list, pipeline);
	rd->draw_list_bind_uniform_set(list, p_uniform_set, 0);
	if (p_constants) {
		rd->draw_list_set_push_constant(list, p_constants, p_constants_size);
	}
	rd->draw_list_draw(list, false, 1, 3);
	rd->draw_list_end();
	rd->draw_command_end_label();
}

} // namespace

void RenderYutrel::setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) {
	p_render_buffers->set_custom_data(SNAME("Yutrel"), Ref<YutrelRenderBufferData>(memnew(YutrelRenderBufferData)));
}

YutrelFrameData RenderYutrel::_build_frame_data(const RenderDataRD &p_data, const Rect2i &p_region) const {
	YutrelFrameData frame;
	const RenderSceneDataRD &scene = *p_data.scene_data;
	const Projection vp = scene.get_cam_projection() * Projection(scene.cam_transform.affine_inverse());
	const Projection inv_vp = vp.inverse();
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			frame.view_projection[row][column] = vp.columns[column][row];
			frame.inv_view_projection[row][column] = inv_vp.columns[column][row];
		}
	}
	const Vector3 to_camera = scene.cam_transform.basis.get_column(2).normalized();
	for (int axis = 0; axis < 3; axis++) {
		frame.camera_position[axis] = scene.cam_transform.origin[axis];
		frame.camera_forward[axis] = to_camera[axis];
	}
	frame.camera_forward[3] = scene.cam_orthogonal;
	frame.viewport_origin[0] = p_region.position.x;
	frame.viewport_origin[1] = p_region.position.y;
	frame.inv_viewport_size[0] = 1.0f / p_region.size.x;
	frame.inv_viewport_size[1] = 1.0f / p_region.size.y;

	auto *lights = RendererRD::LightStorage::get_singleton();
	if (p_data.lights) {
		for (uint32_t i = 0; i < p_data.lights->size() && frame.light_count < std::size(frame.lights); i++) {
			const RID instance = (*p_data.lights)[i];
			const RID light = lights->light_instance_get_base_light(instance);
			if (lights->light_get_type(light) != RSE::LIGHT_DIRECTIONAL || lights->light_directional_get_sky_mode(light) == RSE::LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY) {
				continue;
			}
			const uint32_t cull_mask = lights->light_get_cull_mask(light);
			if (!(cull_mask & scene.camera_visible_layers)) {
				continue;
			}
			const Vector3 direction = lights->light_instance_get_base_transform(instance).basis.get_column(2).normalized();
			const float energy = lights->light_get_param(light, RSE::LIGHT_PARAM_ENERGY) * (lights->light_is_negative(light) ? -1.0f : 1.0f);
			const Color radiance = lights->light_get_color(light).srgb_to_linear() * energy;
			YutrelDirectionalLight &gpu_light = frame.lights[frame.light_count++];
			for (int axis = 0; axis < 3; axis++) {
				gpu_light.direction[axis] = direction[axis];
				gpu_light.radiance[axis] = radiance[axis];
			}
		}
	}
	return frame;
}

void RenderYutrel::_build_draw_list(const RenderDataRD &p_data) {
	draws.clear();
	instances.clear();
	materials.clear();
	material_sets.clear();
#ifdef TOOLS_ENABLED
	editor_gizmos.clear();
	const bool editor_view = Engine::get_singleton()->is_editor_hint();
	constexpr uint32_t scene_layer_mask = (1u << 20) - 1;
#endif
	if (!p_data.instances) {
		return;
	}

	HashMap<RID, int> material_indices;
	auto *meshes = RendererRD::MeshStorage::get_singleton();
	const bool camera_mirrored = p_data.scene_data->cam_transform.basis.determinant() < 0;
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
		const bool mirrored = (determinant < 0) != camera_mirrored;
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

			const RID material_rid = YutrelMaterialBridge::get_surface_material(instance, data.base, surface_index);
			const int *cached_index = material_indices.getptr(material_rid);
			int material_id = cached_index ? *cached_index : -1;
			if (!cached_index) {
				YutrelMaterialBridge::Material material;
				if (material_bridge.get_material(material_rid, material)) {
					material_id = materials.size();
					materials.push_back(material.data);
					material_sets.push_back(material_bridge.get_uniform_set(base_shader.get_shader(), material));
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
	const Ref<YutrelRenderBufferData> custom = buffers->get_custom_data(SNAME("Yutrel"));
	ERR_FAIL_COND_V(custom.is_null(), false);
	r_targets.gbuffer_a = custom->gbuffer_a;
	r_targets.gbuffer_b = custom->gbuffer_b;
	r_targets.gbuffer_c = custom->gbuffer_c;
	r_targets.scene_color = buffers->get_internal_texture();
	r_targets.scene_depth = buffers->get_depth_texture();
	r_targets.output_color = textures->render_target_get_rd_texture(r_targets.render_target);
	ERR_FAIL_COND_V(r_targets.scene_color.is_null() || r_targets.scene_depth.is_null() || r_targets.output_color.is_null() ||
					r_targets.gbuffer_a.is_null() || r_targets.gbuffer_b.is_null() || r_targets.gbuffer_c.is_null(),
			false);
	ERR_FAIL_COND_V(RD::get_singleton()->texture_get_format(r_targets.scene_color).format != RD::DATA_FORMAT_R16G16B16A16_SFLOAT, false);
	auto *framebuffers = FramebufferCacheRD::get_singleton();
	r_targets.base_framebuffer = framebuffers->get_cache(r_targets.scene_color, r_targets.gbuffer_a, r_targets.gbuffer_b, r_targets.gbuffer_c, r_targets.scene_depth);
	r_targets.lighting_framebuffer = framebuffers->get_cache(r_targets.scene_color);
	r_targets.output_framebuffer = framebuffers->get_cache(r_targets.output_color);
	r_targets.output_linear = textures->render_target_is_using_hdr(r_targets.render_target);
	return r_targets.base_framebuffer.is_valid() && r_targets.lighting_framebuffer.is_valid() && r_targets.output_framebuffer.is_valid();
}

bool RenderYutrel::_bake_dfg_lut() {
	RD *rd = RD::get_singleton();
	constexpr uint32_t size = 64;
	if (dfg_lut.is_null()) {
		RD::TextureFormat format;
		format.width = size;
		format.height = size;
		format.format = RD::DATA_FORMAT_R16G16_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		dfg_lut = rd->texture_create(format, {});
		ERR_FAIL_COND_V(dfg_lut.is_null(), false);
		rd->set_resource_name(dfg_lut, "Yutrel DFG LUT");
	}

	const RID shader = dfg_shader.get_shader();
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(shader, 0, RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, dfg_lut));
	ERR_FAIL_COND_V(set.is_null(), false);
	const RID pipeline = rd->compute_pipeline_create(shader);
	ERR_FAIL_COND_V(pipeline.is_null(), false);

	rd->draw_command_begin_label("Yutrel DFG LUT", Color(0.7, 0.3, 0.9));
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, pipeline);
	rd->compute_list_bind_uniform_set(list, set, 0);
	rd->compute_list_dispatch_threads(list, size, size, 1);
	rd->compute_list_end();
	rd->draw_command_end_label();
	rd->free_rid(pipeline);
	dfg_baked_version = dfg_shader.get_version();
	return true;
}

bool RenderYutrel::_prepare_passes() {
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
	if (refresh_shader(lighting_shader, "directional_light.slang")) {
		RD::PipelineColorBlendState blend = RD::PipelineColorBlendState::create_disabled();
		auto &attachment = blend.attachments.write[0];
		attachment.enable_blend = true;
		attachment.src_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.write_a = false;
		lighting_pipeline.setup(lighting_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, blend);
	}
	if (refresh_shader(output_shader, "output.slang")) {
		output_pipeline.setup(output_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	if (refresh_shader(debug_shader, "debug_view.slang")) {
		debug_pipeline.setup(debug_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	refresh_shader(dfg_shader, "dfg_lut.slang");
	if (dfg_shader.get_shader().is_valid() && dfg_baked_version != dfg_shader.get_version()) {
		_bake_dfg_lut();
	}
	return base_shader.get_shader().is_valid() && lighting_shader.get_shader().is_valid() &&
			output_shader.get_shader().is_valid() && debug_shader.get_shader().is_valid() && dfg_baked_version != 0;
}

void RenderYutrel::_record_base_pass(const YutrelPipelineTargets &p_targets, RID p_uniform_set, const Color &p_clear_color) {
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
	auto *sets = UniformSetCacheRD::get_singleton();
	YutrelViewportSettings settings = YutrelViewportSettings::get(targets.render_target);
	if (settings.debug_view == DebugViewMode::FINAL) {
		if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_UNSHADED) {
			settings.debug_view = DebugViewMode::ALBEDO;
		} else if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_NORMAL_BUFFER) {
			settings.debug_view = DebugViewMode::WORLD_NORMAL;
		}
	}

	const YutrelFrameData frame = _build_frame_data(*p_render_data, targets.region);
	_build_draw_list(*p_render_data);
	const bool final = settings.debug_view == DebugViewMode::FINAL;
	const bool lighting_only = settings.debug_view == DebugViewMode::LIGHTING;
	const bool do_lighting = final || lighting_only;

	RID base_set;
	if (!draws.is_empty()) {
		if (frame_buffer.is_null()) {
			frame_buffer = rd->uniform_buffer_create(sizeof(frame));
			ERR_FAIL_COND(frame_buffer.is_null());
			rd->set_resource_name(frame_buffer, "Yutrel Frame UBO");
		}
		ERR_FAIL_COND(rd->buffer_update(frame_buffer, 0, sizeof(frame), &frame) != OK);
		ERR_FAIL_COND(!instance_buffer.upload(instances, "Yutrel Instance SSBO"));
		ERR_FAIL_COND(!material_buffer.upload(materials, "Yutrel Material SSBO"));
		base_set = sets->get_cache(base_shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, frame_buffer),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, material_buffer.get_buffer()));
		ERR_FAIL_COND(base_set.is_null());
	}

	_record_base_pass(targets, base_set, p_default_color.srgb_to_linear());
	if (do_lighting && !draws.is_empty()) {
		const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
		const RID light_set = sets->get_cache(lighting_shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, frame_buffer),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, targets.gbuffer_a),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, targets.gbuffer_b),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, targets.gbuffer_c),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, targets.scene_depth),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, dfg_lut),
				RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 6, sampler));
		draw_fullscreen("Yutrel DirectionalLightPass", Color(0.9, 0.8, 0.3), targets.region, targets.lighting_framebuffer, lighting_pipeline, light_set, nullptr, 0, lighting_only);
	}

	if (final) {
		const RID output_set = sets->get_cache(output_shader.get_shader(), 0, RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, targets.scene_color));
		const YutrelEnvironmentSettings environment = YutrelEnvironmentSettings::get(p_render_data->environment);
		const OutputPushConstant output = { targets.output_linear, uint32_t(environment.tone_mapping), environment.exposure };
		draw_fullscreen("Yutrel OutputPass", Color(0.7, 0.9, 0.2), targets.region, targets.output_framebuffer, output_pipeline, output_set, &output, sizeof(output));
	} else {
		const RID debug_set = sets->get_cache(debug_shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, targets.gbuffer_a),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, targets.gbuffer_b),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, targets.gbuffer_c),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, targets.scene_depth),
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, targets.scene_color));
		const DebugViewPushConstant debug = { targets.output_linear, uint32_t(settings.debug_view) };
		draw_fullscreen("Yutrel DebugViewPass", Color(0.6, 0.5, 0.9), targets.region, targets.output_framebuffer, debug_pipeline, debug_set, &debug, sizeof(debug));
	}
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		editor_gizmos.render(targets, *p_render_data->scene_data, frame);
	}
#endif
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
	dfg_shader.load("dfg_lut.slang");
	if (dfg_shader.get_shader().is_valid()) {
		_bake_dfg_lut();
	}
	print_verbose("Yutrel: Slang deferred renderer created.");
}

RenderYutrel::~RenderYutrel() {
	RD *rd = RD::get_singleton();
	if (dfg_lut.is_valid()) {
		rd->free_rid(dfg_lut);
	}
	if (frame_buffer.is_valid()) {
		rd->free_rid(frame_buffer);
	}
	// This can call base_uniforms_changed(), so release before the base destructor.
	RendererRD::LightStorage::get_singleton()->directional_shadow_atlas_set_size(0);
}

} // namespace RendererSceneRenderImplementation
