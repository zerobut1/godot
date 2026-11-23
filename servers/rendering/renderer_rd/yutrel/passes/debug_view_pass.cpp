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

namespace RendererSceneRenderImplementation {

namespace {

struct DebugViewPushConstant {
	int32_t viewport_origin[2];
	uint32_t output_linear;
	uint32_t mode;
	uint32_t has_primary_shadow;
};

static_assert(sizeof(DebugViewPushConstant) == 20);

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
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, p_targets.ao_texture));
	const DebugViewPushConstant debug = { { p_targets.region.position.x, p_targets.region.position.y }, p_targets.output_linear, uint32_t(p_mode), p_has_primary_shadow };
	draw_fullscreen("Yutrel DebugViewPass", Color(0.6, 0.5, 0.9), p_targets.region, p_targets.output_framebuffer, debug_pipeline, debug_set, &debug, sizeof(debug));
}

} // namespace RendererSceneRenderImplementation
