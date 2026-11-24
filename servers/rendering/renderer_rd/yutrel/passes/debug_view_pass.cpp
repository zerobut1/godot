/**************************************************************************/
/*  debug_view_pass.cpp                                                   */
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

#include "debug_view_pass.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_rt_scene.h"

namespace RendererSceneRenderImplementation {

namespace {

struct DebugViewPushConstant {
	Point2i viewport_origin;
	Size2i render_size;
	Point2i output_origin;
	Size2i output_size;
	uint32_t output_linear;
	uint32_t mode;
	uint32_t has_primary_shadow;
};

static_assert(sizeof(DebugViewPushConstant) == 44);

} // namespace

bool DebugViewPass::prepare() {
	if (refresh_shader(debug_shader, "debug_view.slang")) {
		debug_pipeline.setup(debug_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	return debug_shader.get_shader().is_valid();
}

void DebugViewPass::render(const YutrelPipelineTargets &p_targets, DebugViewMode p_mode, bool p_has_primary_shadow) {
	const RID debug_set = UniformSetCacheRD::get_singleton()->get_cache(debug_shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, p_targets.gbuffer_a),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_b),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_c),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.scene_depth),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.scene_color),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, p_targets.shadow_mask),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, p_targets.ao_texture),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_targets.rt_reflection_texture),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 8, p_targets.velocity));
	const DebugViewPushConstant debug = { p_targets.region.position, p_targets.region.size, p_targets.output_region.position, p_targets.output_region.size, p_targets.output_linear, uint32_t(p_mode), p_has_primary_shadow };
	draw_fullscreen("Yutrel DebugViewPass", Color(0.6, 0.5, 0.9), p_targets.output_region, p_targets.output_framebuffer, debug_pipeline, debug_set, &debug, sizeof(debug));
}

bool DebugViewPass::render_rt_surface(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, YutrelRayTracingScene &p_scene, DebugViewMode p_mode) {
	if (refresh_shader(rt_debug_shader, "rt_debug_view.slang")) {
		rt_debug_pipeline.setup(rt_debug_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	const RID shader = rt_debug_shader.get_shader();
	ERR_FAIL_COND_V(shader.is_null(), false);
	const RID frame_set = UniformSetCacheRD::get_singleton()->get_cache(shader, 0, RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer));
	const RID scene_set = p_scene.get_uniform_set(shader);
	ERR_FAIL_COND_V(frame_set.is_null() || scene_set.is_null(), false);
	struct RTDebugData {
		uint32_t output_linear, mode, has_tlas, padding;
	} push = { p_targets.output_linear, uint32_t(p_mode), p_scene.has_tlas(), 0 };
	static_assert(sizeof(push) == 16);
	draw_fullscreen("Yutrel RTDebugViewPass", Color(0.5, 0.75, 0.9), p_targets.output_region, p_targets.output_framebuffer, rt_debug_pipeline, frame_set, &push, sizeof(push), false, scene_set);
	return true;
}

} // namespace RendererSceneRenderImplementation
