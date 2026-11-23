/**************************************************************************/
/*  rt_reflection_pass.cpp                                                */
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

#include "rt_reflection_pass.h"

#include "screen_space_compute.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

#ifdef TOOLS_ENABLED
#include "core/config/engine.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server_default.h"
#endif

namespace RendererSceneRenderImplementation {

using namespace ScreenSpaceCompute;
using Uniform = RD::Uniform;

RID RTReflectionPass::get_fallback_reflection() {
	if (fallback_reflection.is_null()) {
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
		fallback_reflection = RD::get_singleton()->texture_create(format, {}, { Vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0, 0 } });
		ERR_FAIL_COND_V(fallback_reflection.is_null(), RID());
		RD::get_singleton()->set_resource_name(fallback_reflection, "Yutrel Empty RT Reflection");
	}
	return fallback_reflection;
}

RTReflectionPass::~RTReflectionPass() {
	if (fallback_reflection.is_valid()) {
		RD::get_singleton()->free_rid(fallback_reflection);
	}
}

void RTReflectionPass::History::clear() {
	for (RID *rid : { &reflection_texture, &raw_reflection, &filtered_reflection, &hit_dist[0], &hit_dist[1], &history[0], &history[1], &prev_depth[0], &prev_depth[1], &prev_normal_roughness[0], &prev_normal_roughness[1], &prev_frame_buffer }) {
		if (rid->is_valid()) {
			RD::get_singleton()->free_rid(*rid);
			*rid = RID();
		}
	}
	full_size = Size2i();
	work_size = Size2i();
	has_history = false;
	frame_index = 0;
	history_slot = 0;
}

bool RTReflectionPass::History::ensure_size(Size2i p_full_size, Size2i p_work_size) {
	if (full_size == p_full_size && work_size == p_work_size) {
		return true;
	}
	clear();
	reflection_texture = create_image(p_full_size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel RT Reflection");
	raw_reflection = create_image(p_work_size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel RT Reflection Raw");
	filtered_reflection = create_image(p_work_size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel RT Reflection Filtered");
	prev_frame_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(prev_frame));
	bool valid = reflection_texture.is_valid() && raw_reflection.is_valid() && filtered_reflection.is_valid() && prev_frame_buffer.is_valid();
	for (int i = 0; i < 2; i++) {
		history[i] = create_image(p_work_size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel RT Reflection History");
		prev_depth[i] = create_image(p_work_size, RD::DATA_FORMAT_R32_SFLOAT, "Yutrel RT Reflection Depth");
		prev_normal_roughness[i] = create_image(p_work_size, RD::DATA_FORMAT_R16G16B16A16_UNORM, "Yutrel RT Reflection Normal");
		hit_dist[i] = create_image(p_work_size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RT Reflection Hit Distance");
		valid = valid && hit_dist[i].is_valid() && history[i].is_valid() && prev_depth[i].is_valid() && prev_normal_roughness[i].is_valid();
	}
	if (!valid) {
		clear();
		return false;
	}
	full_size = p_full_size;
	work_size = p_work_size;
	return true;
}

bool RTReflectionPass::render(History &h, const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame,
		const YutrelEnvironmentSettings &p_environment, const LocalVector<DirectionalLightItem> &p_lights, const IBLPass &p_ibl,
		const YutrelSkyParams &p_sky, const DDGIBindings &p_ddgi, YutrelRayTracingScene &p_scene, double p_time_step) {
	const bool half_res = p_environment.rt_reflection_half_resolution;
	const Size2i full_size = p_targets.region.size;
	const Size2i work_size = half_res ? Size2i((full_size.x + 1) / 2, (full_size.y + 1) / 2) : full_size;
	const uint32_t pixel_scale = half_res ? 2 : 1;
	const bool trace_ready = trace.prepare("rt_reflection_trace.slang");
	const bool temporal_ready = temporal.prepare("rt_reflection_temporal.slang");
	const bool denoise_ready = denoise.prepare("rt_reflection_denoise.slang");
	const bool upsample_ready = !half_res || upsample.prepare("rt_reflection_upsample.slang");
	ERR_FAIL_COND_V(!trace_ready || !temporal_ready || !denoise_ready || !upsample_ready || !h.ensure_size(full_size, work_size), false);
	const uint64_t version = trace.shader.get_version() + temporal.shader.get_version() + denoise.shader.get_version() + (half_res ? upsample.shader.get_version() : 0);
	LocalVector<DirectionalLightParams> lights;
	uint64_t lighting_hash = rt_hash_bytes(5381, &p_sky, sizeof(p_sky));
	lighting_hash = rt_hash_bytes(lighting_hash, &p_environment.linear_sky_color, sizeof(Color));
	lighting_hash = hash_djb2_one_64(uint32_t(p_environment.sky_mode), lighting_hash);
	lighting_hash = hash_djb2_one_64(p_environment.sky_texture_rid.get_id(), lighting_hash);
	lighting_hash = hash_djb2_one_64(p_ddgi.volume.get_id(), lighting_hash);
	for (const DirectionalLightItem &light : p_lights) {
		lights.push_back(light.params);
		lighting_hash = rt_hash_bytes(lighting_hash, &light.params, sizeof(light.params));
	}
	if (lights.is_empty()) {
		lights.push_back({});
	}
	ERR_FAIL_COND_V(!light_buffer.upload(lights, "Yutrel RT Reflection Lights"), false);
	const bool reuse = h.has_history && h.lighting_hash == lighting_hash && h.shader_version == version && h.last_half_resolution == half_res &&
			h.last_samples == p_environment.rt_reflection_samples && h.last_max_roughness == p_environment.rt_reflection_max_roughness;
	const uint32_t prev = h.history_slot;
	const uint32_t curr = 1 - prev;
	const uint32_t frame_index = reuse ? h.frame_index : 0;

	RD *rd = RD::get_singleton();
	ERR_FAIL_COND_V(rd->buffer_update(h.prev_frame_buffer, 0, sizeof(p_frame), reuse ? &h.prev_frame : &p_frame) != OK, false);
	auto *sets = UniformSetCacheRD::get_singleton();
	const Uniform frame(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer);
	const Uniform normal(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_b);
	const Uniform depth(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.scene_depth);
	const RID scene_set = p_scene.get_uniform_set(trace.shader.get_shader());
	const RID linear_clamp = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID trace_set = sets->get_cache(trace.shader.get_shader(), 0, frame, normal, depth,
			Uniform(RD::UNIFORM_TYPE_IMAGE, 3, h.raw_reflection),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.gbuffer_a),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, p_targets.gbuffer_c),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, light_buffer.get_buffer()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_ibl.get_source_cubemap()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, p_ibl.get_prefilter_cubemap()),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 9, p_ibl.get_cube_sampler()),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 10, p_ibl.get_sh_buffer()),
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 11, p_ddgi.volume),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 12, p_ddgi.irradiance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 13, p_ddgi.distance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 14, p_ddgi.probe_data),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 15, linear_clamp));
	const RID temporal_set = sets->get_cache(temporal.shader.get_shader(), 0, frame,
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 1, h.prev_frame_buffer),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_b),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.scene_depth),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.raw_reflection),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.history[prev]),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, h.prev_depth[prev]),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, h.prev_normal_roughness[prev]),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 8, h.history[curr]),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 9, h.prev_depth[curr]),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 10, h.prev_normal_roughness[curr]),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 11, p_targets.gbuffer_c),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 12, h.hit_dist[prev]),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 13, h.hit_dist[curr]));
	// Temporal has consumed raw_reflection; reuse it for the middle filter step.
	const RID filter_images[] = { h.history[curr], h.filtered_reflection, h.raw_reflection, half_res ? h.filtered_reflection : h.reflection_texture };
	RID filter_sets[3];
	for (int i = 0; i < 3; i++) {
		filter_sets[i] = sets->get_cache(denoise.shader.get_shader(), 0, frame,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, h.prev_normal_roughness[curr]), Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, h.prev_depth[curr]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, filter_images[i]), Uniform(RD::UNIFORM_TYPE_IMAGE, 4, filter_images[i + 1]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.hit_dist[curr]));
		ERR_FAIL_COND_V(filter_sets[i].is_null(), false);
	}
	ERR_FAIL_COND_V(scene_set.is_null() || trace_set.is_null() || temporal_set.is_null(), false);
	RID upsample_set;
	if (half_res) {
		upsample_set = sets->get_cache(upsample.shader.get_shader(), 0, frame, normal, depth,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, h.filtered_reflection), Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.prev_depth[curr]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.prev_normal_roughness[curr]), Uniform(RD::UNIFORM_TYPE_IMAGE, 6, h.reflection_texture),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_targets.gbuffer_c), Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, p_targets.gbuffer_a));
		ERR_FAIL_COND_V(upsample_set.is_null(), false);
	}

	struct TraceParams {
		Size2i full_size;
		uint32_t pixel_scale;
		float max_roughness;
		uint32_t samples, frame_index, has_tlas, light_count;
		YutrelSkyParams sky;
		float max_mip;
	} trace_params = { full_size, pixel_scale, p_environment.rt_reflection_max_roughness, uint32_t(p_environment.rt_reflection_samples), frame_index, p_scene.has_tlas(), p_lights.size(), p_sky, p_ibl.get_prefilter_max_mip() };
	struct TemporalParams {
		Size2i full_size;
		uint32_t pixel_scale, has_history;
		float max_history, dt_scale;
	} temporal_params = { full_size, pixel_scale, reuse, 16.0f, p_time_step > 0 ? CLAMP(float(p_time_step * 60.0), 1.0f / 60.0f, 4.0f) : 1.0f };
	struct FilterParams {
		Size2i full_size;
		uint32_t pixel_scale;
		int32_t step_size;
		uint32_t is_final;
	};
	static_assert(sizeof(TraceParams) == 48 && sizeof(TemporalParams) == 24 && sizeof(FilterParams) == 20);

	rd->draw_command_begin_label("Yutrel RT Reflection", Color(0.5, 0.6, 0.75));
	RENDER_TIMESTAMP("Yutrel RT Reflection Trace");
	dispatch(trace.pipeline, trace_set, trace_params, work_size, scene_set);
	RENDER_TIMESTAMP("Yutrel RT Reflection Temporal");
	dispatch(temporal.pipeline, temporal_set, temporal_params, work_size);
	RENDER_TIMESTAMP("Yutrel RT Reflection Denoise");
	for (int i = 0; i < 3; i++) {
		dispatch(denoise.pipeline, filter_sets[i], FilterParams{ full_size, pixel_scale, 1 << i, i == 2 }, work_size);
	}
	if (half_res) {
		RENDER_TIMESTAMP("Yutrel RT Reflection Upsample");
		dispatch(upsample.pipeline, upsample_set, p_environment.rt_reflection_max_roughness, full_size);
	}
	rd->draw_command_end_label();
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		const bool stationary = reuse && h.last_scene_hash == p_scene.get_scene_hash() &&
				memcmp(p_frame.view_projection, h.prev_frame.view_projection, sizeof(p_frame.view_projection)) == 0;
		h.last_scene_hash = p_scene.get_scene_hash();
		h.converged_frames = stationary ? MIN(h.converged_frames + 1, 16u) : 0;
		if (h.converged_frames < 16 && DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
			RenderingServerDefault::redraw_request();
		}
	}
#endif
	h.prev_frame = p_frame;
	h.shader_version = version;
	h.lighting_hash = lighting_hash;
	h.last_samples = p_environment.rt_reflection_samples;
	h.last_max_roughness = p_environment.rt_reflection_max_roughness;
	h.last_half_resolution = half_res;
	h.frame_index = frame_index + 1;
	h.history_slot = curr;
	h.has_history = true;
	return true;
}

} // namespace RendererSceneRenderImplementation
