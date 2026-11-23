/**************************************************************************/
/*  ddgi_pass.cpp                                                         */
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

#include "ddgi_pass.h"

#ifdef TOOLS_ENABLED
#include "core/config/engine.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server_default.h"
#endif

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

namespace {

RID create_atlas(RD::DataFormat p_format, int p_width, int p_height, int p_layers, const char *p_name) {
	RD::TextureFormat format;
	format.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	format.format = p_format;
	format.width = p_width;
	format.height = p_height;
	format.array_layers = p_layers;
	format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	RID texture = RD::get_singleton()->texture_create(format, {});
	if (texture.is_valid()) {
		RD::get_singleton()->set_resource_name(texture, p_name);
	}
	return texture;
}

RID linear_sampler() {
	return RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
}

using Uniform = RD::Uniform;

void dispatch(Span<char> p_name, RID p_pipeline, RID p_set, Vector3i p_groups) {
	RD *rd = RD::get_singleton();
	rd->draw_command_begin_label(p_name, Color(0.3f, 0.65f, 0.45f));
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, p_pipeline);
	rd->compute_list_bind_uniform_set(list, p_set, 0);
	rd->compute_list_dispatch(list, p_groups.x, p_groups.y, p_groups.z);
	rd->compute_list_end();
	rd->draw_command_end_label();
}

} // namespace

void DDGIPass::History::clear() {
	for (RID rid : { bindings.volume, bindings.irradiance, bindings.distance, bindings.probe_data, ray_data }) {
		if (rid.is_valid()) {
			RD::get_singleton()->free_rid(rid);
		}
	}
	bindings = {};
	ray_data = RID();
	config_version = 0;
	rays = 0;
	counts = Vector3i();
}

bool DDGIPass::update(History &h, const YutrelDDGISettings &v, const LocalVector<DirectionalLightItem> &p_lights, const LocalLightPass &p_local,
		const IBLPass &p_ibl, const YutrelSkyParams &p_sky, YutrelRayTracingScene &p_scene, double p_time_step) {
	const bool trace_ready = trace.prepare("ddgi_probe_trace.slang");
	const bool irr_ready = blend_irradiance.prepare("ddgi_blend_irradiance.slang");
	const bool dist_ready = blend_distance.prepare("ddgi_blend_distance.slang");
	const bool relocation_ready = relocate.prepare("ddgi_probe_relocation.slang");
	const bool classification_ready = classify.prepare("ddgi_probe_classification.slang");
	ERR_FAIL_COND_V(!trace_ready || !irr_ready || !dist_ready || !relocation_ready || !classification_ready, false);

	RD *rd = RD::get_singleton();
	const Vector3i counts = v.probe_counts;
	if (h.counts != counts) {
		h.clear();
		h.bindings.volume = rd->uniform_buffer_create(sizeof(DDGIVolumeData));
		h.bindings.irradiance = create_atlas(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, counts.x * 8, counts.z * 8, counts.y, "Yutrel DDGI Irradiance");
		h.bindings.distance = create_atlas(RD::DATA_FORMAT_R16G16_SFLOAT, counts.x * 16, counts.z * 16, counts.y, "Yutrel DDGI Distance");
		h.bindings.probe_data = create_atlas(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, counts.x, counts.z, counts.y, "Yutrel DDGI Probe Data");
		ERR_FAIL_COND_V(h.bindings.volume.is_null() || h.bindings.irradiance.is_null() || h.bindings.distance.is_null() || h.bindings.probe_data.is_null(), false);
		rd->set_resource_name(h.bindings.volume, "Yutrel DDGI Volume");
		h.counts = counts;
	}
	if (h.rays != v.rays_per_probe) {
		const RID rays = create_atlas(RD::DATA_FORMAT_R16G16B16A16_SFLOAT, v.rays_per_probe, counts.x * counts.z, counts.y, "Yutrel DDGI Ray Data");
		ERR_FAIL_COND_V(rays.is_null(), false);
		if (h.ray_data.is_valid()) {
			rd->free_rid(h.ray_data);
		}
		h.ray_data = rays;
		h.rays = v.rays_per_probe;
	}

	const uint64_t version = trace.shader.get_version() + blend_irradiance.shader.get_version() + blend_distance.shader.get_version() +
			relocate.shader.get_version() + classify.shader.get_version();
	const bool uses_fixed = (v.probe_relocation || v.probe_classification) && uint32_t(v.rays_per_probe) > DDGI_NUM_FIXED_RAYS;
	DDGIVolumeData gpu = {};
	gpu.reset_history = h.config_version != v.config_version || h.shader_version != version || h.used_fixed_rays != uses_fixed;
	if (gpu.reset_history) {
		h.rotations.seed(0x44444749);
	}
	for (int axis = 0; axis < 3; ++axis) {
		gpu.origin[axis] = v.origin[axis];
		gpu.spacing[axis] = v.size[axis] / (counts[axis] - 1);
		gpu.probe_counts[axis] = counts[axis];
	}
	// Uniform unit quaternion (Shoemake), with a reproducible sequence per history.
	const float u = h.rotations.randf();
	const float a = Math::TAU * h.rotations.randf();
	const float b = Math::TAU * h.rotations.randf();
	gpu.ray_rotation[0] = Math::sqrt(1 - u) * Math::sin(a);
	gpu.ray_rotation[1] = Math::sqrt(1 - u) * Math::cos(a);
	gpu.ray_rotation[2] = Math::sqrt(u) * Math::sin(b);
	gpu.ray_rotation[3] = Math::sqrt(u) * Math::cos(b);
	gpu.rays_per_probe = v.rays_per_probe;
	gpu.max_ray_distance = v.max_ray_distance;
	// Normalize hysteresis to a 60 FPS reference step so high-FPS viewports stay temporally smooth.
	const float dt_scale = p_time_step > 0.0 ? CLAMP(float(p_time_step * 60.0), 1.0f / 60.0f, 4.0f) : 1.0f;
	gpu.hysteresis = v.hysteresis > 0.0f ? Math::pow(v.hysteresis, dt_scale) : 0.0f;
	gpu.normal_bias = v.normal_bias;
	gpu.view_bias = v.view_bias;
	gpu.distance_exponent = v.distance_exponent;
	gpu.irradiance_gamma = v.irradiance_gamma;
	gpu.irradiance_threshold = v.irradiance_threshold;
	gpu.brightness_threshold = v.brightness_threshold;
	gpu.backface_threshold = v.backface_threshold;
	gpu.intensity = v.intensity;
	gpu.infinite_bounces = v.infinite_bounces;
	gpu.probe_preview_radius = v.probe_preview_radius;
	gpu.min_frontface_distance = v.min_frontface_distance;
	gpu.fixed_ray_backface_threshold = v.fixed_ray_backface_threshold;
	gpu.relocation_enabled = v.probe_relocation;
	gpu.classification_enabled = v.probe_classification;
	gpu.active = 1;
	ERR_FAIL_COND_V(rd->buffer_update(h.bindings.volume, 0, sizeof(gpu), &gpu) != OK, false);

	LocalVector<DirectionalLightParams> lights;
	for (const DirectionalLightItem &light : p_lights) {
		lights.push_back(light.params);
	}
	if (lights.is_empty()) {
		lights.push_back({});
	}
	ERR_FAIL_COND_V(!light_buffer.upload(lights, "Yutrel DDGI Lights"), false);

	auto *sets = UniformSetCacheRD::get_singleton();
	const Uniform ubo(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, h.bindings.volume);
	const Uniform samples(RD::UNIFORM_TYPE_TEXTURE, 1, h.ray_data);
	const RID trace_set = sets->get_cache(trace.shader.get_shader(), 0, ubo,
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, light_buffer.get_buffer()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_ibl.get_source_cubemap()),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 3, p_ibl.get_cube_sampler()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.bindings.irradiance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.bindings.distance),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 6, linear_sampler()),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 7, h.ray_data),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, h.bindings.probe_data), p_local.light_uniform());
	const RID scene_set = p_scene.get_uniform_set(trace.shader.get_shader());
	const Uniform probes(RD::UNIFORM_TYPE_TEXTURE, 3, h.bindings.probe_data);
	const RID irr_set = sets->get_cache(blend_irradiance.shader.get_shader(), 0, ubo, samples, Uniform(RD::UNIFORM_TYPE_IMAGE, 2, h.bindings.irradiance), probes);
	const RID dist_set = sets->get_cache(blend_distance.shader.get_shader(), 0, ubo, samples, Uniform(RD::UNIFORM_TYPE_IMAGE, 2, h.bindings.distance), probes);
	RID relocate_set, classify_set;
	const bool update_probes = uses_fixed || (gpu.reset_history && uint32_t(v.rays_per_probe) > DDGI_NUM_FIXED_RAYS);
	if (update_probes) {
		const Uniform data(RD::UNIFORM_TYPE_IMAGE, 2, h.bindings.probe_data);
		relocate_set = sets->get_cache(relocate.shader.get_shader(), 0, ubo, samples, data);
		classify_set = sets->get_cache(classify.shader.get_shader(), 0, ubo, samples, data);
		ERR_FAIL_COND_V(relocate_set.is_null() || classify_set.is_null(), false);
	}
	ERR_FAIL_COND_V(trace_set.is_null() || scene_set.is_null() || irr_set.is_null() || dist_set.is_null(), false);

	struct TraceParams {
		uint32_t lights, has_tlas;
		YutrelSkyParams sky;
		uint32_t local_count;
	} params = { p_lights.size(), p_scene.has_tlas(), p_sky, p_local.get_count() };
	static_assert(sizeof(TraceParams) == 24);
	RENDER_TIMESTAMP("Yutrel DDGI Trace");
	rd->draw_command_begin_label("Yutrel DDGIProbeTrace", Color(0.3f, 0.8f, 0.6f));
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, trace.pipeline);
	rd->compute_list_bind_uniform_set(list, trace_set, 0);
	rd->compute_list_bind_uniform_set(list, scene_set, 1);
	rd->compute_list_set_push_constant(list, &params, sizeof(params));
	rd->compute_list_dispatch(list, (v.rays_per_probe + 63) / 64, counts.x * counts.z, counts.y);
	rd->compute_list_end();
	rd->draw_command_end_label();

	// Trace and both blends use last frame's positions and states. Publish new probe data afterwards.
	const Vector3i tiles(counts.x, counts.z, counts.y);
	RENDER_TIMESTAMP("Yutrel DDGI Blend");
	dispatch("Yutrel DDGIProbeBlendIrradiance", blend_irradiance.pipeline, irr_set, tiles);
	dispatch("Yutrel DDGIProbeBlendDistance", blend_distance.pipeline, dist_set, tiles);
	if (update_probes) {
		const Vector3i groups((counts.x * counts.y * counts.z + 31) / 32, 1, 1);
		RENDER_TIMESTAMP("Yutrel DDGI Relocate/Classify");
		dispatch("Yutrel DDGIProbeRelocation", relocate.pipeline, relocate_set, groups);
		dispatch("Yutrel DDGIProbeClassification", classify.pipeline, classify_set, groups);
	}
	h.used_fixed_rays = uses_fixed;
	h.config_version = v.config_version;
	h.shader_version = version;
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint() && DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
		RenderingServerDefault::redraw_request();
	}
#endif
	return true;
}

void DDGIPass::draw_probes(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const History &p_history, const YutrelEnvironmentSettings &p_environment) {
	if (refresh_shader(debug_shader, "ddgi_probe_debug.slang")) {
		RD::PipelineDepthStencilState depth;
		depth.enable_depth_test = true;
		depth.enable_depth_write = true;
		depth.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;
		debug_pipeline.setup(debug_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, depth, RD::PipelineColorBlendState::create_disabled());
	}
	RD *rd = RD::get_singleton();
	const RID fb = FramebufferCacheRD::get_singleton()->get_cache(p_targets.output_color, p_targets.scene_depth);
	ERR_FAIL_COND(debug_shader.get_shader().is_null() || fb.is_null());
	const RID pipeline = debug_pipeline.get_render_pipeline(RD::INVALID_FORMAT_ID, rd->framebuffer_get_format(fb));
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(debug_shader.get_shader(), 0,
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 1, p_history.bindings.volume),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_history.bindings.irradiance),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 3, linear_sampler()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_history.bindings.probe_data));
	ERR_FAIL_COND(set.is_null() || pipeline.is_null());
	struct OutputParams {
		uint32_t linear, tone_mapping;
		float exposure;
	} params = { p_targets.output_linear, uint32_t(p_environment.tone_mapping), p_environment.exposure };
	static_assert(sizeof(OutputParams) == 12);
	rd->draw_command_begin_label("Yutrel DDGI Probes", Color(0.4f, 0.85f, 0.65f));
	const RD::DrawListID list = rd->draw_list_begin(fb, RD::DRAW_DEFAULT_ALL, {}, 0, 0, p_targets.region);
	rd->draw_list_bind_render_pipeline(list, pipeline);
	rd->draw_list_bind_uniform_set(list, set, 0);
	rd->draw_list_set_push_constant(list, &params, sizeof(params));
	rd->draw_list_draw(list, false, p_history.counts.x * p_history.counts.y * p_history.counts.z, 12 * 8 * 6);
	rd->draw_list_end();
	rd->draw_command_end_label();
}

} // namespace RendererSceneRenderImplementation
