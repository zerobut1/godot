/**************************************************************************/
/*  rtao_pass.cpp                                                        */
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

#include "rtao_pass.h"

#include "screen_space_compute.h"

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

RID RTAOPass::get_fallback_ao() {
	if (fallback_ao.is_null()) {
		const YutrelScopedLabel label("Yutrel White AO Upload");
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R16_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
		fallback_ao = RD::get_singleton()->texture_create(format, {}, { Vector<uint8_t>{ 0x00, 0x3c } });
		ERR_FAIL_COND_V(fallback_ao.is_null(), RID());
		RD::get_singleton()->set_resource_name(fallback_ao, "Yutrel White AO");
	}
	return fallback_ao;
}

RTAOPass::~RTAOPass() {
	if (fallback_ao.is_valid()) {
		RD::get_singleton()->free_rid(fallback_ao);
	}
}

void RTAOPass::History::clear() {
	for (RID *rid : { &ao_texture, &raw_ao, &denoise_temp, &filtered_ao, &history[0], &history[1] }) {
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

bool RTAOPass::History::ensure_size(Size2i p_full_size, Size2i p_work_size) {
	if (full_size == p_full_size && work_size == p_work_size) {
		return true;
	}
	clear();
	ao_texture = create_image(p_full_size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RTAO");
	raw_ao = create_image(p_work_size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RTAO Raw");
	denoise_temp = create_image(p_work_size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RTAO Filter Temp");
	bool valid = ao_texture.is_valid() && raw_ao.is_valid() && denoise_temp.is_valid();
	if (p_work_size != p_full_size) {
		filtered_ao = create_image(p_work_size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RTAO Filtered");
		valid = valid && filtered_ao.is_valid();
	}
	for (int i = 0; i < 2; i++) {
		history[i] = create_image(p_work_size, RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel RTAO History");
		valid = valid && history[i].is_valid();
	}
	if (!valid) {
		clear();
		return false;
	}
	full_size = p_full_size;
	work_size = p_work_size;
	return true;
}

bool RTAOPass::render(History &h, const RTGeometryPass::View &p_geo, const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame,
		const YutrelEnvironmentSettings &p_environment, YutrelRayTracingScene &p_scene, double p_time_step) {
	const bool half_res = p_environment.rtao_half_resolution;
	const Size2i full_size = p_targets.region.size;
	const Size2i work_size = p_geo.work_size;
	const uint32_t pixel_scale = p_geo.pixel_scale;
	const bool trace_ready = trace.prepare("rtao_trace.slang");
	const bool temporal_ready = temporal.prepare("rtao_temporal.slang");
	const bool denoise_ready = denoise.prepare("rtao_denoise.slang");
	const bool upsample_ready = !half_res || upsample.prepare("rtao_upsample.slang");
	ERR_FAIL_COND_V(!trace_ready || !temporal_ready || !denoise_ready || !upsample_ready || !h.ensure_size(full_size, work_size), false);
	const uint64_t version = p_geo.shader_version + trace.shader.get_version() + temporal.shader.get_version() + denoise.shader.get_version() + (half_res ? upsample.shader.get_version() : 0);
	const bool reuse = h.has_history && p_geo.has_history && h.shader_version == version && h.last_half_resolution == half_res &&
			h.last_samples == p_environment.rtao_samples && h.last_radius == p_environment.rtao_radius;
	const uint32_t prev = h.history_slot;
	const uint32_t curr = 1 - prev;
	const uint32_t frame_index = reuse ? h.frame_index : 0;

	const Color label_color(0.5, 0.6, 0.75);
	const YutrelScopedLabel pass_label("Yutrel RTAO", label_color);
	auto *sets = UniformSetCacheRD::get_singleton();
	const Uniform frame(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer);
	const Uniform normal(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_b);
	const Uniform depth(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.scene_depth);
	const RID scene_set = p_scene.get_uniform_set(trace.shader.get_shader());
	const RID trace_set = sets->get_cache(trace.shader.get_shader(), 0, frame,
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_geo.curr_surface), depth, Uniform(RD::UNIFORM_TYPE_IMAGE, 3, h.raw_ao));
	const RID temporal_set = sets->get_cache(temporal.shader.get_shader(), 0, frame,
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 1, p_geo.prev_frame_buffer),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_geo.curr_surface), Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_geo.prev_surface),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.raw_ao), Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.history[prev]),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, p_geo.velocity), Uniform(RD::UNIFORM_TYPE_IMAGE, 7, h.history[curr]));
	// A 1x1 viewport has no separate filtered image. Its raw AO is free after temporal.
	const RID filtered = h.filtered_ao.is_valid() ? h.filtered_ao : h.raw_ao;
	const int filter_count = half_res ? 2 : 3;
	const RID filter_images[] = { h.history[curr], half_res ? h.denoise_temp : h.ao_texture, half_res ? filtered : h.denoise_temp, h.ao_texture };
	RID filter_sets[3];
	for (int i = 0; i < filter_count; i++) {
		filter_sets[i] = sets->get_cache(denoise.shader.get_shader(), 0, frame,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_geo.curr_surface),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, filter_images[i]), Uniform(RD::UNIFORM_TYPE_IMAGE, 4, filter_images[i + 1]));
		ERR_FAIL_COND_V(filter_sets[i].is_null(), false);
	}
	ERR_FAIL_COND_V(scene_set.is_null() || trace_set.is_null() || temporal_set.is_null(), false);
	RID upsample_set;
	if (half_res) {
		upsample_set = sets->get_cache(upsample.shader.get_shader(), 0, frame, normal, depth,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, filtered), Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_geo.curr_surface),
				Uniform(RD::UNIFORM_TYPE_IMAGE, 6, h.ao_texture));
		ERR_FAIL_COND_V(upsample_set.is_null(), false);
	}

	struct TraceParams {
		Size2i full_size;
		uint32_t pixel_scale;
		float radius;
		uint32_t samples, frame_index, has_tlas;
	} trace_params = { full_size, pixel_scale, p_environment.rtao_radius, uint32_t(p_environment.rtao_samples), frame_index, p_scene.has_tlas() };
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
		float intensity;
	};
	static_assert(sizeof(TraceParams) == 28 && sizeof(TemporalParams) == 24 && sizeof(FilterParams) == 24);

	RENDER_TIMESTAMP("Yutrel RTAO Trace");
	dispatch("RTAO Trace", trace.pipeline, trace_set, trace_params, work_size, scene_set, label_color);
	RENDER_TIMESTAMP("Yutrel RTAO Temporal");
	dispatch("RTAO Temporal", temporal.pipeline, temporal_set, temporal_params, work_size, RID(), label_color);
	RENDER_TIMESTAMP("Yutrel RTAO Denoise");
	static constexpr Span<char> denoise_labels[] = { "RTAO Denoise 0", "RTAO Denoise 1", "RTAO Denoise 2" };
	for (int i = 0; i < filter_count; i++) {
		dispatch(denoise_labels[i], denoise.pipeline, filter_sets[i], FilterParams{ full_size, pixel_scale, 1 << i, !half_res && i == filter_count - 1, p_environment.rtao_intensity }, work_size, RID(), label_color);
	}
	if (half_res) {
		RENDER_TIMESTAMP("Yutrel RTAO Upsample");
		dispatch("RTAO Upsample", upsample.pipeline, upsample_set, p_environment.rtao_intensity, full_size, RID(), label_color);
	}
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		const bool stationary = reuse && memcmp(p_frame.unjittered_view_projection, p_geo.prev_frame->unjittered_view_projection, sizeof(p_frame.unjittered_view_projection)) == 0 &&
				h.last_scene_hash == p_scene.get_scene_hash() && h.last_intensity == p_environment.rtao_intensity;
		h.converged_frames = stationary ? MIN(h.converged_frames + 1, 16u) : 0;
		if (h.converged_frames < 16 && DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
			RenderingServerDefault::redraw_request();
		}
		h.last_scene_hash = p_scene.get_scene_hash();
		h.last_intensity = p_environment.rtao_intensity;
	}
#endif
	h.shader_version = version;
	h.last_samples = p_environment.rtao_samples;
	h.last_radius = p_environment.rtao_radius;
	h.last_half_resolution = half_res;
	h.frame_index = frame_index + 1;
	h.history_slot = curr;
	h.has_history = true;
	return true;
}

} // namespace RendererSceneRenderImplementation
