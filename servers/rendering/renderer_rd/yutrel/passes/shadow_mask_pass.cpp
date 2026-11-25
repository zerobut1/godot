/**************************************************************************/
/*  shadow_mask_pass.cpp                                                  */
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

#include "shadow_mask_pass.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

bool ShadowMaskPass::prepare() {
	if (refresh_shader(shader, "shadow_mask.slang")) {
		pipeline.setup(shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	RD *rd = RD::get_singleton();
	if (comparison_sampler.is_null()) {
		RD::SamplerState state;
		state.min_filter = state.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		state.repeat_u = state.repeat_v = state.repeat_w = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
		state.enable_compare = true;
		state.compare_op = RD::COMPARE_OP_GREATER_OR_EQUAL;
		comparison_sampler = rd->sampler_create(state);
	}
	if (shadow_buffer.is_null()) {
		shadow_buffer = rd->uniform_buffer_create(sizeof(CSMShadowData));
		if (shadow_buffer.is_valid()) {
			rd->set_resource_name(shadow_buffer, "Yutrel CSM Shadow UBO");
		}
	}
	return shader.get_shader().is_valid() && comparison_sampler.is_valid() && shadow_buffer.is_valid();
}

void ShadowMaskPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_depth_array, const CSMShadowData &p_shadow, bool p_show_cascades) {
	const YutrelScopedLabel pass_label("Yutrel ShadowMaskPass", Color(0.4, 0.5, 0.8));
	ERR_FAIL_COND(RD::get_singleton()->buffer_update(shadow_buffer, 0, sizeof(p_shadow), &p_shadow) != OK);
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_a),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_b),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.gbuffer_c),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.scene_depth),
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 5, shadow_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, p_depth_array),
			RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 7, comparison_sampler));
	const uint32_t debug = p_show_cascades;
	const RID framebuffer = p_show_cascades ? p_targets.lighting_framebuffer : FramebufferCacheRD::get_singleton()->get_cache(p_targets.shadow_mask);
	draw_fullscreen({}, Color(0.4, 0.5, 0.8), p_targets.region, framebuffer, pipeline, set, &debug, sizeof(debug));
}

ShadowMaskPass::~ShadowMaskPass() {
	RD *rd = RD::get_singleton();
	if (shadow_buffer.is_valid()) {
		rd->free_rid(shadow_buffer);
	}
	if (comparison_sampler.is_valid()) {
		rd->free_rid(comparison_sampler);
	}
}

} // namespace RendererSceneRenderImplementation
