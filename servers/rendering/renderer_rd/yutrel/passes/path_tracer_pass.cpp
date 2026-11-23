/**************************************************************************/
/*  path_tracer_pass.cpp                                                  */
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

#include "path_tracer_pass.h"

#include "core/config/engine.h"
#include "servers/display/display_server.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_default.h"

namespace RendererSceneRenderImplementation {

bool PathTracerPass::is_supported() {
	return YutrelRayTracingScene::is_supported(1);
}

bool PathTracerPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame,
		const LocalVector<DirectionalLightItem> &p_lights, const YutrelEnvironmentSettings &p_environment,
		const IBLPass &p_ibl, YutrelRayTracingScene &p_scene, uint64_t &r_state_hash, uint32_t &r_sample_count) {
	RD *rd = RD::get_singleton();
	if (refresh_shader(shader, "path_tracer.slang")) {
		pipeline = RID(); // RD freed the old shader's dependent pipeline.
	}
	if (pipeline.is_null() && shader.get_shader().is_valid()) {
		pipeline = rd->compute_pipeline_create(shader.get_shader());
	}
	ERR_FAIL_COND_V(pipeline.is_null(), false);

	uint64_t hash = rt_hash_bytes(p_scene.get_scene_hash(), &p_frame, sizeof(p_frame));
	hash = hash_djb2_one_64(shader.get_version(), hash);
	hash = hash_djb2_one_64(uint32_t(p_environment.sky_mode), hash);
	hash = hash_djb2_one_64(p_environment.sky_texture_rid.get_id(), hash);
	hash = rt_hash_bytes(hash, &p_environment.linear_sky_color, sizeof(Color));
	hash = rt_hash_bytes(hash, &p_environment.sky_intensity, sizeof(float));
	hash = rt_hash_bytes(hash, &p_environment.sky_rotation_radians, sizeof(float));
	for (const DirectionalLightItem &light : p_lights) {
		hash = rt_hash_bytes(hash, &light.params, sizeof(light.params));
	}
	if (hash != r_state_hash) {
		r_sample_count = 0;
		r_state_hash = hash;
	}
	constexpr uint32_t MAX_SAMPLES = 256;
	if (r_sample_count >= MAX_SAMPLES) {
		return true;
	}
	LocalVector<DirectionalLightParams> lights;
	for (const DirectionalLightItem &light : p_lights) {
		lights.push_back(light.params);
	}
	if (lights.is_empty()) {
		lights.push_back({});
	}
	ERR_FAIL_COND_V(!light_buffer.upload(lights, "Yutrel Path Tracer Lights"), false);

	struct Push {
		uint32_t origin[2], size[2];
		uint32_t sample, lights, has_tlas, has_sky;
		YutrelSkyParams sky;
	};
	static_assert(sizeof(Push) == 44);
	const Rect2i &region = p_targets.region;
	const Push params = {
		{ uint32_t(region.position.x), uint32_t(region.position.y) },
		{ uint32_t(region.size.x), uint32_t(region.size.y) },
		r_sample_count, p_lights.size(), p_scene.has_tlas(), p_ibl.get_source_cubemap().is_valid(),
		{ p_environment.sky_intensity, Math::sin(p_environment.sky_rotation_radians), Math::cos(p_environment.sky_rotation_radians) }
	};
	const RID scene_set = p_scene.get_uniform_set(shader.get_shader());
	const RID pass_set = UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, light_buffer.get_buffer()),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_ibl.get_source_cubemap()),
			RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 3, p_ibl.get_cube_sampler()),
			RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, p_targets.path_tracer_accum));
	ERR_FAIL_COND_V(scene_set.is_null() || pass_set.is_null(), false);

	rd->draw_command_begin_label("Yutrel PathTracerPass", Color(0.25, 0.8, 0.65));
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, pipeline);
	rd->compute_list_bind_uniform_set(list, pass_set, 0);
	rd->compute_list_bind_uniform_set(list, scene_set, 1);
	rd->compute_list_set_push_constant(list, &params, sizeof(params));
	rd->compute_list_dispatch(list, (region.size.x + 7) / 8, (region.size.y + 7) / 8, 1);
	rd->compute_list_end();
	rd->draw_command_end_label();
	++r_sample_count;
#ifdef TOOLS_ENABLED
	if (r_sample_count < MAX_SAMPLES && Engine::get_singleton()->is_editor_hint() &&
			DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
		RenderingServerDefault::redraw_request();
	}
#endif
	return true;
}

PathTracerPass::~PathTracerPass() {
	if (pipeline.is_valid()) {
		RD::get_singleton()->free_rid(pipeline);
	}
}

} // namespace RendererSceneRenderImplementation
