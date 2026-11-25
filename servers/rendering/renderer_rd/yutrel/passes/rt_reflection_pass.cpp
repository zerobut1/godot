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
		const YutrelScopedLabel label("Yutrel Empty RT Reflection Upload");
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
	for (RID rid : { fallback_reflection, dummy_surface, dummy_velocity }) {
		if (rid.is_valid()) {
			RD::get_singleton()->free_rid(rid);
		}
	}
}

void RTReflectionPass::History::clear() {
	for (RID *rid : { &quad_offset, &raw_reflection, &reprojected, &variance, &average, &filtered, &history[0], &history[1], &guide[0], &guide[1], &stats[0], &stats[1],
				 &full_reflection[0], &full_reflection[1], &mirror_guide[0], &mirror_guide[1], &prev_frame_buffer, &ray_list, &indirect_buffer, &mirror_flags }) {
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

bool RTReflectionPass::History::ensure_size(Size2i p_full_size, Size2i p_work_size, bool p_half_res) {
	if (full_size == p_full_size && work_size == p_work_size && full_reflection[0].is_valid() == p_half_res) {
		return true;
	}
	clear();
	RD *rd = RD::get_singleton();
	const uint64_t list_bytes = (uint64_t(p_work_size.x) * p_work_size.y + 1) * sizeof(uint32_t);
	ERR_FAIL_COND_V(list_bytes > UINT32_MAX || p_work_size.x > 65536 || p_work_size.y > 65536, false);
	Vector<uint8_t> zero_flags;
	zero_flags.resize_initialized(2 * sizeof(uint32_t));
	mirror_flags = rd->storage_buffer_create(zero_flags.size(), zero_flags);
	if (mirror_flags.is_valid()) {
		rd->set_resource_name(mirror_flags, "Yutrel RT Reflection Mirror Flags");
	}
	ray_list = rd->storage_buffer_create(list_bytes);
	indirect_buffer = rd->storage_buffer_create(3 * sizeof(uint32_t), {}, RD::STORAGE_BUFFER_USAGE_DISPATCH_INDIRECT);
	constexpr RD::DataFormat rgba = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	quad_offset = create_image(p_work_size, RD::DATA_FORMAT_R8_UINT, "Yutrel RT Reflection Quad Offset");
	raw_reflection = create_image(p_work_size, rgba, "Yutrel RT Reflection Raw");
	reprojected = create_image(p_work_size, rgba, "Yutrel RT Reflection Reprojected");
	filtered = create_image(p_work_size, rgba, "Yutrel RT Reflection Prefiltered");
	average = create_image((p_work_size + Size2i(7, 7)) / 8, rgba, "Yutrel RT Reflection Tile Average");
	variance = create_image(p_work_size, RD::DATA_FORMAT_R16_SFLOAT, "Yutrel RT Reflection Variance");
	prev_frame_buffer = rd->uniform_buffer_create(sizeof(prev_frame));
	if (ray_list.is_valid()) {
		rd->set_resource_name(ray_list, "Yutrel RT Reflection Ray List");
	}
	if (indirect_buffer.is_valid()) {
		rd->set_resource_name(indirect_buffer, "Yutrel RT Reflection Indirect");
	}
	if (prev_frame_buffer.is_valid()) {
		rd->set_resource_name(prev_frame_buffer, "Yutrel RT Reflection PrevFrame UBO");
	}
	bool valid = quad_offset.is_valid() && raw_reflection.is_valid() && reprojected.is_valid() && filtered.is_valid() && average.is_valid() && variance.is_valid() && prev_frame_buffer.is_valid() && ray_list.is_valid() && indirect_buffer.is_valid() && mirror_flags.is_valid();
	for (int i = 0; i < 2; i++) {
		history[i] = create_image(p_work_size, rgba, "Yutrel RT Reflection History");
		guide[i] = create_image(p_work_size, rgba, "Yutrel RT Reflection Surface");
		stats[i] = create_image(p_work_size, RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel RT Reflection Variance and Count");
		valid = valid && history[i].is_valid() && guide[i].is_valid() && stats[i].is_valid();
		if (p_half_res) {
			full_reflection[i] = create_image(p_full_size, rgba, "Yutrel RT Reflection Full Resolution");
			mirror_guide[i] = create_image(p_full_size, rgba, "Yutrel RT Reflection Mirror Surface");
			valid = valid && full_reflection[i].is_valid() && mirror_guide[i].is_valid();
		}
	}
	if (!valid) {
		clear();
		return false;
	}
	full_size = p_full_size;
	work_size = p_work_size;
	return true;
}

bool RTReflectionPass::render(History &h, const RTGeometryPass::View *p_half_geo, const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame,
		const YutrelEnvironmentSettings &p_environment, const LocalVector<DirectionalLightItem> &p_lights, const LocalLightPass &p_local, const IBLPass &p_ibl,
		const YutrelSkyParams &p_sky, const DDGIBindings &p_ddgi, YutrelRayTracingScene &p_scene, double p_time_step) {
	const bool half_res = p_environment.rt_reflection_half_resolution;
	const Size2i full_size = p_targets.region.size;
	const Size2i work_size = half_res ? Size2i((full_size.x + 1) / 2, (full_size.y + 1) / 2) : full_size;
	const uint32_t pixel_scale = half_res ? 2 : 1;
	const Color label_color(0.5, 0.6, 0.75);
	const YutrelScopedLabel pass_label("Yutrel RT Reflection", label_color);
	const bool classify_ready = classify.prepare("rt_reflection_classify.slang");
	const bool trace_ready = trace.prepare("rt_reflection_trace.slang");
	const bool temporal_ready = temporal.prepare("rt_reflection_temporal.slang");
	const bool reproject_ready = reproject.prepare("rt_reflection_reproject.slang");
	const bool prefilter_ready = prefilter.prepare("rt_reflection_prefilter.slang");
	const bool upsample_ready = !half_res || upsample.prepare("rt_reflection_upsample.slang");
	ERR_FAIL_COND_V(!classify_ready || !trace_ready || !temporal_ready || !reproject_ready || !prefilter_ready || !upsample_ready || !h.ensure_size(full_size, work_size, half_res), false);
	const bool has_half_geo = half_res && p_half_geo;
	if (!has_half_geo) {
		if (dummy_surface.is_null()) {
			dummy_surface = create_image(Size2i(1, 1), RD::DATA_FORMAT_R32G32_UINT, "Yutrel RT Reflection Unused Surface");
		}
		if (dummy_velocity.is_null()) {
			dummy_velocity = create_image(Size2i(1, 1), RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel RT Reflection Unused Velocity");
		}
		ERR_FAIL_COND_V(dummy_surface.is_null() || dummy_velocity.is_null(), false);
	}
	const uint64_t version = (has_half_geo ? p_half_geo->shader_version : 0) + classify.shader.get_version() + trace.shader.get_version() + temporal.shader.get_version() + reproject.shader.get_version() + prefilter.shader.get_version() + (half_res ? upsample.shader.get_version() : 0);
	LocalVector<DirectionalLightParams> lights;
	uint64_t lighting_hash = rt_hash_bytes(p_local.get_hash(), &p_sky, sizeof(p_sky));
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
	const Uniform raw(RD::UNIFORM_TYPE_IMAGE, 3, h.raw_reflection);
	const Uniform roughness(RD::UNIFORM_TYPE_TEXTURE, 5, p_targets.gbuffer_c);
	const RID classify_set = sets->get_cache(classify.shader.get_shader(), 0, frame, depth, raw,
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.gbuffer_a), roughness,
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, h.ray_list), Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 7, h.indirect_buffer),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 8, h.quad_offset), Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 9, h.mirror_flags));
	const RID scene_set = p_scene.get_uniform_set(trace.shader.get_shader());
	const RID linear_clamp = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID trace_set = sets->get_cache(trace.shader.get_shader(), 0, frame, normal, depth, raw,
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 4, h.ray_list), roughness,
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 6, light_buffer.get_buffer()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_ibl.get_source_cubemap()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, p_ibl.get_prefilter_cubemap()),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 9, p_ibl.get_cube_sampler()),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 10, p_ibl.get_sh_buffer()),
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 11, p_ddgi.volume),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 12, p_ddgi.irradiance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 13, p_ddgi.distance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 14, p_ddgi.probe_data),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 15, linear_clamp), p_local.light_uniform(), Uniform(RD::UNIFORM_TYPE_TEXTURE, 20, h.quad_offset));
	const Uniform prev_frame(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 1, h.prev_frame_buffer);
	const RID reproject_set = sets->get_cache(reproject.shader.get_shader(), 0, frame, prev_frame,
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_b), Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.scene_depth),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.raw_reflection), Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.history[prev]),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, h.guide[prev]), Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, h.stats[prev]),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, p_targets.gbuffer_c), Uniform(RD::UNIFORM_TYPE_IMAGE, 9, h.guide[curr]),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 10, h.reprojected), Uniform(RD::UNIFORM_TYPE_IMAGE, 11, h.variance), Uniform(RD::UNIFORM_TYPE_IMAGE, 12, h.average),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 13, h.quad_offset), Uniform(RD::UNIFORM_TYPE_TEXTURE, 14, p_targets.velocity),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 15, has_half_geo ? p_half_geo->curr_surface : dummy_surface),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 16, has_half_geo ? p_half_geo->velocity : dummy_velocity));
	const RID prefilter_set = sets->get_cache(prefilter.shader.get_shader(), 0, Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, h.raw_reflection),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, h.guide[curr]), Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, h.variance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, h.average), Uniform(RD::UNIFORM_TYPE_IMAGE, 4, h.filtered), Uniform(RD::UNIFORM_TYPE_SAMPLER, 5, linear_clamp));
	const RID temporal_set = sets->get_cache(temporal.shader.get_shader(), 0, Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, h.raw_reflection),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, h.guide[curr]), Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, h.filtered),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, h.reprojected), Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.average),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 5, h.history[curr]), Uniform(RD::UNIFORM_TYPE_IMAGE, 6, h.stats[curr]), Uniform(RD::UNIFORM_TYPE_SAMPLER, 7, linear_clamp));
	ERR_FAIL_COND_V(classify_set.is_null() || scene_set.is_null() || trace_set.is_null() || reproject_set.is_null() || prefilter_set.is_null() || temporal_set.is_null(), false);
	RID upsample_set;
	if (half_res) {
		upsample_set = sets->get_cache(upsample.shader.get_shader(), 0, frame, normal, depth,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, h.history[curr]), Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, h.guide[curr]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, h.raw_reflection), Uniform(RD::UNIFORM_TYPE_IMAGE, 6, h.full_reflection[curr]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_targets.gbuffer_c), Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, p_targets.gbuffer_a),
				Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 9, h.prev_frame_buffer), Uniform(RD::UNIFORM_TYPE_TEXTURE, 10, h.full_reflection[prev]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 11, h.mirror_guide[prev]), Uniform(RD::UNIFORM_TYPE_IMAGE, 12, h.mirror_guide[curr]),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 13, p_targets.velocity), Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 14, h.mirror_flags));
		ERR_FAIL_COND_V(upsample_set.is_null(), false);
	}

	// Large full-resolution viewports can exceed the device's X dispatch limit.
	const uint64_t rays_per_row = 64 * rd->limit_get(RD::LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X);
	const uint32_t dispatch_rows = Math::division_round_up(uint64_t(work_size.x) * work_size.y, rays_per_row);
	struct ClassifyParams {
		Size2i full_size;
		uint32_t pixel_scale;
		float max_roughness;
		uint32_t has_tlas, dispatch_rows, frame_index, curr_slot;
	} classify_params = { full_size, pixel_scale, p_environment.rt_reflection_max_roughness, p_scene.has_tlas(), dispatch_rows, frame_index, curr };
	struct TraceParams {
		Size2i full_size;
		uint32_t pixel_scale, dispatch_rows;
		uint32_t samples, frame_index, light_count;
		YutrelSkyParams sky;
		float max_mip;
		uint32_t local_count;
	} trace_params = { full_size, pixel_scale, dispatch_rows, uint32_t(p_environment.rt_reflection_samples), frame_index, p_lights.size(), p_sky, p_ibl.get_prefilter_max_mip(), p_local.get_count() };
	const bool same_scene = reuse && h.last_scene_hash == p_scene.get_scene_hash();
	const bool stationary = reuse && memcmp(p_frame.unjittered_view_projection, h.prev_frame.unjittered_view_projection, sizeof(p_frame.unjittered_view_projection)) == 0;
	struct ReprojectParams {
		Size2i full_size;
		uint32_t scale, has_history, stationary, has_half_geo;
	} reproject_params = { full_size, pixel_scale, reuse, stationary, has_half_geo };
	struct ResolveParams {
		Size2i size;
		float dt_scale;
	} resolve_params = { work_size, p_time_step > 0 ? CLAMP(float(p_time_step * 60.0), 1.0f / 60.0f, 4.0f) : 1.0f };
	struct UpsampleParams {
		float max_roughness;
		uint32_t index, has_history, stationary, scene_changed, curr_slot;
	} upsample_params = { p_environment.rt_reflection_max_roughness, frame_index, reuse, stationary, !same_scene, curr };
	static_assert(sizeof(ClassifyParams) == 32 && sizeof(TraceParams) == 48 && sizeof(ReprojectParams) == 24 && sizeof(ResolveParams) == 12 && sizeof(UpsampleParams) == 24);

	// RD tracks indirect and shader reads separately. Keep the count in ray_list[0]
	// so trace can read it while the command processor consumes the indirect buffer.
	const uint32_t indirect_args[] = { 0, dispatch_rows, 1 };
	ERR_FAIL_COND_V(rd->buffer_update(h.indirect_buffer, 0, sizeof(indirect_args), indirect_args) != OK, false);
	ERR_FAIL_COND_V(rd->buffer_clear(h.ray_list, 0, sizeof(uint32_t)) != OK, false);
	ERR_FAIL_COND_V(rd->buffer_clear(h.mirror_flags, curr * sizeof(uint32_t), sizeof(uint32_t)) != OK, false);
	RENDER_TIMESTAMP("Yutrel RT Reflection Classify");
	dispatch("RT Reflection Classify", classify.pipeline, classify_set, classify_params, work_size, RID(), label_color);
	RENDER_TIMESTAMP("Yutrel RT Reflection Trace");
	{
		const YutrelScopedLabel trace_label("RT Reflection Trace", label_color);
		const RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, trace.pipeline);
		rd->compute_list_bind_uniform_set(list, trace_set, 0);
		rd->compute_list_bind_uniform_set(list, scene_set, 1);
		rd->compute_list_set_push_constant(list, &trace_params, sizeof(trace_params));
		rd->compute_list_dispatch_indirect(list, h.indirect_buffer, 0);
		rd->compute_list_end();
	}
	RENDER_TIMESTAMP("Yutrel RT Reflection Reproject");
	dispatch("RT Reflection Reproject", reproject.pipeline, reproject_set, reproject_params, work_size, RID(), label_color);
	RENDER_TIMESTAMP("Yutrel RT Reflection Prefilter");
	dispatch("RT Reflection Prefilter", prefilter.pipeline, prefilter_set, work_size, work_size, RID(), label_color);
	RENDER_TIMESTAMP("Yutrel RT Reflection Temporal");
	dispatch("RT Reflection Temporal", temporal.pipeline, temporal_set, resolve_params, work_size, RID(), label_color);
	if (half_res) {
		RENDER_TIMESTAMP("Yutrel RT Reflection Upsample");
		dispatch("RT Reflection Upsample", upsample.pipeline, upsample_set, upsample_params, full_size, RID(), label_color);
	}
#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		h.converged_frames = stationary && same_scene ? MIN(h.converged_frames + 1, 32u) : 0;
		if (h.converged_frames < 32 && DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
			RenderingServerDefault::redraw_request();
		}
	}
#endif
	h.last_scene_hash = p_scene.get_scene_hash();
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
