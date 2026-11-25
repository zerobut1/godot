/**************************************************************************/
/*  rt_shadow_pass.cpp                                                    */
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

#include "rt_shadow_pass.h"

#include "directional_light_pass.h"
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

namespace {

Size2i work_size(Size2i p_full_size, bool p_half_resolution) {
	return p_half_resolution ? Size2i((p_full_size.x + 1) / 2, (p_full_size.y + 1) / 2) : p_full_size;
}

void free_resource(RID &r_rid) {
	if (r_rid.is_valid()) {
		RD::get_singleton()->free_rid(r_rid);
		r_rid = RID();
	}
}

struct ShadowGrid {
	Size2i full_size;
	uint32_t pixel_scale, padding;
};

struct TraceParams {
	float direction[4];
	ShadowGrid grid;
	uint32_t samples, frame_index, has_tlas;
};
struct TemporalParams {
	ShadowGrid grid;
	uint32_t has_history;
	float max_history, dt_scale;
};
struct FilterParams {
	ShadowGrid grid;
	int32_t step_size;
	uint32_t is_final;
	float opacity;
};
struct UpsampleParams {
	ShadowGrid grid;
	float opacity;
};
static_assert(sizeof(TraceParams) == 44 && sizeof(TemporalParams) == 28 && sizeof(FilterParams) == 28 && sizeof(UpsampleParams) == 20);

} // namespace

void RTShadowPass::LightHistory::clear() {
	free_resource(visibility[0]);
	free_resource(visibility[1]);
}

void RTShadowPass::History::invalidate() {
	has_history = false;
	for (KeyValue<RID, LightHistory> &entry : lights) {
		entry.value.clear();
	}
	lights.clear();
}

void RTShadowPass::History::clear() {
	invalidate();
	for (RID *rid : { &raw_shadow, &denoise_temp, &filtered_shadow }) {
		free_resource(*rid);
	}
	full_size = Size2i();
	slot = 0;
}

bool RTShadowPass::History::ensure_size(Size2i p_full_size, bool p_half_resolution, bool p_soft) {
	const Size2i size = work_size(p_full_size, p_half_resolution);
	if (full_size != p_full_size || half_resolution != p_half_resolution) {
		clear();
		raw_shadow = create_image(size, RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel RT Shadow Raw");
		ERR_FAIL_COND_V(raw_shadow.is_null(), false);
		full_size = p_full_size;
		half_resolution = p_half_resolution;
	}
	if (p_soft && denoise_temp.is_null()) {
		denoise_temp = create_image(size, RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel RT Shadow Filter Temp");
		if (p_half_resolution) {
			filtered_shadow = create_image(size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RT Shadow Filtered");
		}
		bool valid = denoise_temp.is_valid() && (!p_half_resolution || filtered_shadow.is_valid());
		if (!valid) {
			clear();
			return false;
		}
	}
	return true;
}

void RTShadowPass::History::finish_frame() {
	// A debug view can stop after the first light. Only histories paired with
	// this frame's depth and normal images can be reprojected next frame.
	for (auto it = lights.begin(); it;) {
		auto current = it;
		++it;
		if (current->value.rendered) {
			current->value.rendered = false;
		} else {
			current->value.clear();
			lights.remove(current);
		}
	}
	has_history = true;
	slot = 1 - slot;
}

bool RTShadowPass::render(History &h, const RTGeometryPass::View &p_geo, const DirectionalLightItem &p_light, const YutrelPipelineTargets &p_targets,
		RID p_frame_buffer, const YutrelFrameData &p_frame, const YutrelEnvironmentSettings &p_environment,
		YutrelRayTracingScene &p_scene, double p_time_step) {
	const bool half_res = p_environment.shadow_rt_half_resolution;
	const bool is_soft = p_light.params.direction[3] > 0;
	const bool resolve = half_res || !is_soft;
	const Size2i full_size = p_targets.region.size;
	const Size2i size = p_geo.work_size;
	const ShadowGrid grid = { full_size, p_geo.pixel_scale, 0 };
	ERR_FAIL_COND_V(!trace.prepare("rt_shadow_trace.slang"), false);
	ERR_FAIL_COND_V(resolve && !upsample.prepare("rt_shadow_upsample.slang"), false);
	ERR_FAIL_COND_V(is_soft && (!temporal.prepare("rt_shadow_temporal.slang") || !denoise.prepare("rt_shadow_denoise.slang")), false);
	ERR_FAIL_COND_V(!h.ensure_size(full_size, half_res, is_soft), false);

	using Uniform = RD::Uniform;
	auto *sets = UniformSetCacheRD::get_singleton();
	const Uniform frame(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer);
	const Uniform normal(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_b);
	const Uniform depth(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.scene_depth);
	const RID scene_set = p_scene.get_uniform_set(trace.shader.get_shader());
	const RID trace_set = sets->get_cache(trace.shader.get_shader(), 0, frame,
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_geo.curr_surface), depth, Uniform(RD::UNIFORM_TYPE_IMAGE, 3, h.raw_shadow));
	ERR_FAIL_COND_V(scene_set.is_null() || trace_set.is_null(), false);
	RID upsample_set;
	if (resolve) {
		upsample_set = sets->get_cache(upsample.shader.get_shader(), 0, frame, normal, depth,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, is_soft ? h.filtered_shadow : h.raw_shadow),
				Uniform(RD::UNIFORM_TYPE_IMAGE, 4, p_targets.shadow_mask), Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, p_geo.curr_surface));
		ERR_FAIL_COND_V(upsample_set.is_null(), false);
	}

	TraceParams trace_params = { {}, grid, uint32_t(p_environment.shadow_rt_samples), 0, p_scene.has_tlas() };
	memcpy(trace_params.direction, p_light.params.direction, sizeof(trace_params.direction));
	const Color label_color(0.4, 0.5, 0.8);
	const YutrelScopedLabel pass_label("Yutrel RT Shadow", label_color);
	if (is_soft) {
		const uint32_t prev = h.slot, curr = 1 - prev;
		LightHistory &light = h.lights[p_light.instance];
		if (light.visibility[0].is_null()) {
			for (RID &image : light.visibility) {
				image = create_image(size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel RT Shadow Light History");
			}
			if (light.visibility[0].is_null() || light.visibility[1].is_null()) {
				light.clear();
				return false;
			}
		}
		const uint64_t version = p_geo.shader_version + trace.shader.get_version() + temporal.shader.get_version() + denoise.shader.get_version() + (resolve ? upsample.shader.get_version() : 0);
		const bool reuse = h.has_history && p_geo.has_history && light.shader_version == version && light.samples == p_environment.shadow_rt_samples &&
				memcmp(light.direction, trace_params.direction, sizeof(light.direction)) == 0;
		trace_params.frame_index = reuse ? light.frame_index : 0;
		const RID temporal_set = sets->get_cache(temporal.shader.get_shader(), 0, frame,
				Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 1, p_geo.prev_frame_buffer),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_geo.curr_surface), Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_geo.prev_surface),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.raw_shadow), Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, light.visibility[prev]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, p_geo.velocity), Uniform(RD::UNIFORM_TYPE_IMAGE, 7, light.visibility[curr]));
		ERR_FAIL_COND_V(temporal_set.is_null(), false);

		// The raw image is free after temporal. Reuse it for the second RG16F filter output.
		const RID inputs[] = { light.visibility[curr], h.denoise_temp, h.raw_shadow };
		const RID output = half_res ? h.filtered_shadow : p_targets.shadow_mask;
		RID filter_sets[3];
		for (int i = 0; i < 3; ++i) {
			filter_sets[i] = sets->get_cache(denoise.shader.get_shader(), 0, frame,
					Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_geo.curr_surface),
					Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, inputs[i]), Uniform(RD::UNIFORM_TYPE_IMAGE, 4, i == 1 ? h.raw_shadow : h.denoise_temp),
					Uniform(RD::UNIFORM_TYPE_IMAGE, 5, output));
			ERR_FAIL_COND_V(filter_sets[i].is_null(), false);
		}
		RENDER_TIMESTAMP("Yutrel RT Shadow Trace");
		dispatch("RT Shadow Trace", trace.pipeline, trace_set, trace_params, size, scene_set, label_color);
		RENDER_TIMESTAMP("Yutrel RT Shadow Temporal");
		const float dt_scale = p_time_step > 0 ? CLAMP(float(p_time_step * 60.0), 1.0f / 60.0f, 4.0f) : 1.0f;
		dispatch("RT Shadow Temporal", temporal.pipeline, temporal_set, TemporalParams{ grid, reuse, 16.0f, dt_scale }, size, RID(), label_color);
		RENDER_TIMESTAMP("Yutrel RT Shadow Denoise");
		static constexpr Span<char> denoise_labels[] = { "RT Shadow Denoise 0", "RT Shadow Denoise 1", "RT Shadow Denoise 2" };
		for (int i = 0; i < 3; ++i) {
			dispatch(denoise_labels[i], denoise.pipeline, filter_sets[i], FilterParams{ grid, 1 << i, i == 2, p_light.shadow_opacity }, size, RID(), label_color);
		}
#ifdef TOOLS_ENABLED
		if (Engine::get_singleton()->is_editor_hint() && DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
			const bool stationary = reuse && light.last_scene_hash == p_scene.get_scene_hash() &&
					memcmp(p_frame.unjittered_view_projection, p_geo.prev_frame->unjittered_view_projection, sizeof(p_frame.unjittered_view_projection)) == 0;
			light.converged_frames = stationary ? MIN(light.converged_frames + 1, 16u) : 0;
			if (light.converged_frames < 16) {
				RenderingServerDefault::redraw_request();
			}
			light.last_scene_hash = p_scene.get_scene_hash();
		}
#endif
		memcpy(light.direction, trace_params.direction, sizeof(light.direction));
		light.samples = p_environment.shadow_rt_samples;
		light.frame_index = trace_params.frame_index + 1;
		light.shader_version = version;
		light.rendered = true;
	} else {
		RENDER_TIMESTAMP("Yutrel RT Shadow Trace");
		dispatch("RT Shadow Trace", trace.pipeline, trace_set, trace_params, size, scene_set, label_color);
	}
	if (resolve) {
		RENDER_TIMESTAMP("Yutrel RT Shadow Upsample");
		dispatch("RT Shadow Upsample", upsample.pipeline, upsample_set, UpsampleParams{ grid, p_light.shadow_opacity }, full_size, RID(), label_color);
	}
	return true;
}

} // namespace RendererSceneRenderImplementation
