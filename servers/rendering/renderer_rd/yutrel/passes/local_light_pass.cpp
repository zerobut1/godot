/**************************************************************************/
/*  local_light_pass.cpp                                                  */
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

#include "local_light_pass.h"

#include "../yutrel_rt_scene.h"

#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/renderer_scene_cull.h"

namespace RendererSceneRenderImplementation {

namespace {

constexpr uint32_t MAX_LOCAL_LIGHTS = 1024;
constexpr uint32_t GRID_X = 16, GRID_Y = 9, GRID_Z = 24;
constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 128;

struct alignas(16) LocalLightData {
	enum Flags { IS_SPOT = 1,
		HAS_SHADOW = 2 };
	float position[3], inv_range;
	float radiance[3], attenuation;
	float direction[3], spot_cos_outer;
	float spot_attenuation, shadow_opacity, range;
	uint32_t flags;
};
static_assert(sizeof(LocalLightData) == 64);

struct alignas(16) ClusterParams {
	float view_matrix[4][4], inv_projection[4][4];
	uint32_t grid_size[3] = { GRID_X, GRID_Y, GRID_Z };
	uint32_t light_count;
	float z_near, z_far, slice_scale;
	uint32_t is_ortho;
	float viewport_origin[2], inv_viewport_size[2];
};
static_assert(sizeof(ClusterParams) == 176);

void append_light(RID p_instance, uint32_t p_layers, LocalVector<LocalLightData> &r_items, bool &r_shadowed) {
	auto *lights = RendererRD::LightStorage::get_singleton();
	const RID light = lights->light_instance_get_base_light(p_instance);
	const RSE::LightType type = lights->light_get_type(light);
	if ((type != RSE::LIGHT_OMNI && type != RSE::LIGHT_SPOT) || !(lights->light_get_cull_mask(light) & p_layers)) {
		return;
	}
	const float range = lights->light_get_param(light, RSE::LIGHT_PARAM_RANGE);
	const float energy = lights->light_get_param(light, RSE::LIGHT_PARAM_ENERGY) * (lights->light_is_negative(light) ? -1.0f : 1.0f);
	const Color radiance = lights->light_get_color(light).srgb_to_linear() * energy;
	if (range <= 0 || (radiance.r == 0 && radiance.g == 0 && radiance.b == 0)) {
		return;
	}

	const Transform3D transform = lights->light_instance_get_base_transform(p_instance);
	LocalLightData item = {};
	item.range = range;
	item.inv_range = 1.0f / range;
	item.attenuation = lights->light_get_param(light, RSE::LIGHT_PARAM_ATTENUATION);
	item.spot_cos_outer = -2.0f;
	for (int i = 0; i < 3; ++i) {
		item.position[i] = transform.origin[i];
		item.radiance[i] = radiance[i];
	}
	if (type == RSE::LIGHT_SPOT) {
		item.flags |= LocalLightData::IS_SPOT;
		const Vector3 direction = -transform.basis.get_column(2).normalized();
		for (int i = 0; i < 3; ++i) {
			item.direction[i] = direction[i];
		}
		item.spot_cos_outer = Math::cos(Math::deg_to_rad(CLAMP(lights->light_get_param(light, RSE::LIGHT_PARAM_SPOT_ANGLE), 0.01f, 89.9f)));
		item.spot_attenuation = 1.0f / MAX(lights->light_get_param(light, RSE::LIGHT_PARAM_SPOT_ATTENUATION), 0.0001f);
	}
	if (lights->light_has_shadow(light)) {
		item.flags |= LocalLightData::HAS_SHADOW;
		item.shadow_opacity = lights->light_get_param(light, RSE::LIGHT_PARAM_SHADOW_OPACITY);
		r_shadowed |= item.shadow_opacity > 0;
	}
	r_items.push_back(item);
}

bool prepare_lighting(YutrelShader &r_shader, PipelineCacheRD &r_pipeline, const char *p_path) {
	if (refresh_shader(r_shader, p_path)) {
		auto blend = RD::PipelineColorBlendState::create_disabled();
		auto &attachment = blend.attachments.write[0];
		attachment.enable_blend = true;
		attachment.src_color_blend_factor = attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.write_a = false;
		r_pipeline.setup(r_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, blend);
	}
	return r_shader.get_shader().is_valid();
}

} // namespace

bool LocalLightPass::collect(const RenderDataRD &p_data) {
	LocalVector<LocalLightData> items;
	shadowed = false;
	const uint32_t layers = p_data.scene_data->camera_visible_layers;
	const auto *scenario = RendererSceneCull::singleton->scenario_owner.get_or_null(YutrelRayTracingScene::get_scenario(p_data));
	if (scenario) {
		using InstanceData = RendererSceneCull::InstanceData;
		for (uint32_t i = 0; i < scenario->instance_data.size() && items.size() < MAX_LOCAL_LIGHTS; ++i) {
			const InstanceData &data = scenario->instance_data[i];
			if ((data.flags & InstanceData::FLAG_BASE_TYPE_MASK) == RSE::INSTANCE_LIGHT && (data.layer_mask & layers) && data.instance->visible) {
				const uint32_t visibility = data.flags & InstanceData::FLAG_VISIBILITY_DEPENDENCY_NEEDS_CHECK;
				if (visibility == InstanceData::FLAG_VISIBILITY_DEPENDENCY_HIDDEN || visibility == InstanceData::FLAG_VISIBILITY_DEPENDENCY_HIDDEN_CLOSE_RANGE) {
					continue;
				}
				append_light(RID::from_uint64(data.instance_data_rid), layers, items, shadowed);
			}
		}
	} else if (p_data.lights) {
		for (uint32_t i = 0; i < p_data.lights->size() && items.size() < MAX_LOCAL_LIGHTS; ++i) {
			append_light((*p_data.lights)[i], layers, items, shadowed);
		}
	}
	light_count = items.size();
	light_hash = rt_hash_bytes(5381, items.ptr(), light_count * sizeof(LocalLightData));
	if (items.is_empty()) {
		items.push_back({});
	}
	return local_light_buffer.upload(items, "Yutrel Local Lights");
}

bool LocalLightPass::build_clusters(const RenderDataRD &p_data, const Rect2i &p_region) {
	RD *rd = RD::get_singleton();
	constexpr uint32_t cells = GRID_X * GRID_Y * GRID_Z;
	if (grid.is_null()) {
		grid = rd->storage_buffer_create(cells * 2 * sizeof(uint32_t));
	}
	if (indices.is_null()) {
		indices = rd->storage_buffer_create(cells * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32_t));
	}
	if (params_buffer.is_null()) {
		params_buffer = rd->uniform_buffer_create(sizeof(ClusterParams));
	}
	ERR_FAIL_COND_V(grid.is_null() || indices.is_null() || params_buffer.is_null(), false);

	const RenderSceneDataRD &scene = *p_data.scene_data;
	const Projection view(scene.cam_transform.affine_inverse());
	const Projection inv_projection = scene.get_cam_projection().inverse();
	ClusterParams params = {};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			params.view_matrix[row][col] = view.columns[col][row];
			params.inv_projection[row][col] = inv_projection.columns[col][row];
		}
	}
	params.light_count = light_count;
	params.is_ortho = scene.cam_orthogonal;
	params.z_near = scene.cam_orthogonal ? scene.z_near : MAX(scene.z_near, 0.00001f);
	params.z_far = MAX(scene.z_far, params.z_near + 0.0001f);
	params.slice_scale = GRID_Z / (params.is_ortho ? params.z_far - params.z_near : Math::log(params.z_far / params.z_near));
	for (int axis = 0; axis < 2; ++axis) {
		params.viewport_origin[axis] = p_region.position[axis];
		params.inv_viewport_size[axis] = 1.0f / p_region.size[axis];
	}
	ERR_FAIL_COND_V(rd->buffer_update(params_buffer, 0, sizeof(params), &params) != OK, false);
	if (!light_count) {
		return true;
	}
	ERR_FAIL_COND_V(!cluster_build.prepare("cluster_build.slang"), false);
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(cluster_build.shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, params_buffer), light_uniform(),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, grid),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, indices));
	ERR_FAIL_COND_V(set.is_null(), false);
	RENDER_TIMESTAMP("Yutrel Local Light Cluster");
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, cluster_build.pipeline);
	rd->compute_list_bind_uniform_set(list, set, 0);
	rd->compute_list_dispatch_threads(list, GRID_X, GRID_Y, GRID_Z);
	rd->compute_list_end();
	return true;
}

RID LocalLightPass::get_clustered_set(RID p_shader, LocalVector<RD::Uniform> p_uniforms) const {
	p_uniforms.push_back(light_uniform());
	p_uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 17, grid));
	p_uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 18, indices));
	p_uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 19, params_buffer));
	return UniformSetCacheRD::get_singleton()->get_cache_vec(p_shader, 0, p_uniforms);
}

void LocalLightPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_dfg_lut, YutrelRayTracingScene *p_scene, bool p_clear) {
	ERR_FAIL_COND(!prepare_lighting(shader, pipeline, "local_light.slang"));
	if (p_scene && !prepare_lighting(rt_shader, rt_pipeline, "local_light_rt.slang")) {
		p_scene = nullptr;
	}
	const RID active = p_scene ? rt_shader.get_shader() : shader.get_shader();
	const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID set = get_clustered_set(active, {
													  RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
													  RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_a),
													  RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_b),
													  RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.gbuffer_c),
													  RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.scene_depth),
													  RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, p_dfg_lut),
													  RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 6, sampler),
											  });
	const RID scene_set = p_scene ? p_scene->get_uniform_set(active) : RID();
	ERR_FAIL_COND(set.is_null() || (p_scene && scene_set.is_null()));
	RENDER_TIMESTAMP("Yutrel Local Lights");
	draw_fullscreen("Yutrel Local Lights", Color(1, 0.7, 0.3), p_targets.region, p_targets.lighting_framebuffer, p_scene ? rt_pipeline : pipeline, set, nullptr, 0, p_clear, scene_set);
}

LocalLightPass::~LocalLightPass() {
	for (RID buffer : { grid, indices, params_buffer }) {
		if (buffer.is_valid()) {
			RD::get_singleton()->free_rid(buffer);
		}
	}
}

} // namespace RendererSceneRenderImplementation
