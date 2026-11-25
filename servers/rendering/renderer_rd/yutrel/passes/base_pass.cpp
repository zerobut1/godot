/**************************************************************************/
/*  base_pass.cpp                                                         */
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

#include "base_pass.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

bool BasePass::prepare() {
	const bool base_ready = YutrelGeometryBatch::prepare();
	if (refresh_shader(depth_shader, "depth_prepass.slang")) {
		RD::PipelineDepthStencilState depth;
		depth.enable_depth_test = true;
		depth.enable_depth_write = true;
		depth.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;
		setup_cull_pipelines(depth_pipelines, depth_shader.get_shader(), depth, RD::PipelineColorBlendState::create_disabled(0));
	}
	return base_ready && depth_shader.get_shader().is_valid();
}

RID BasePass::_get_frame_set(RID p_shader, RID p_frame_buffer) const {
	return UniformSetCacheRD::get_singleton()->get_cache(p_shader, 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, material_buffer.get_buffer()));
}

struct BasePassDraw {
	uint32_t draw_id;
};

bool BasePass::_render_depth(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_material_sampler) {
	RENDER_TIMESTAMP("Yutrel Depth Prepass");
	const YutrelScopedLabel pass_label("Yutrel Depth Prepass", Color(0.3, 0.5, 0.8));
	RD *rd = RD::get_singleton();
	const RID depth_set = _get_frame_set(depth_shader.get_shader(), p_frame_buffer);
	ERR_FAIL_COND_V(depth_set.is_null(), false);
	LocalVector<RID> depth_material_sets;
	for (RID albedo : material_albedos) {
		const RID set = UniformSetCacheRD::get_singleton()->get_cache(depth_shader.get_shader(), 1,
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, albedo), RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 1, p_material_sampler));
		ERR_FAIL_COND_V(set.is_null(), false);
		depth_material_sets.push_back(set);
	}

	draws.sort_custom<FrontToBack>();
	const RD::FramebufferFormatID format = rd->framebuffer_get_format(p_targets.depth_framebuffer);
	for (DrawItem &item : draws) {
		resolve_draw(item, format, depth_pipelines[item.mirrored][item.double_sided], item.material.alpha_scissor);
	}
	const RD::DrawListID list = rd->draw_list_begin(p_targets.depth_framebuffer, RD::DRAW_CLEAR_DEPTH, {}, 0.0f, 0, p_targets.region, RDD::BreadcrumbMarker::OPAQUE_PASS);
	rd->draw_list_bind_uniform_set(list, depth_set, 0);
	BasePassDraw draw;
	draw_range(list, 0, draws.size(), depth_material_sets, draw);
	rd->draw_list_end();
	return true;
}

bool BasePass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_material_sampler, const Color &p_clear_color, RenderingServerTypes::RenderInfo *p_render_info) {
	RD *rd = RD::get_singleton();
	RID base_set;
	if (has_draws()) {
		ERR_FAIL_COND_V(!upload_geometry(), false);
		base_set = _get_frame_set(shader.get_shader(), p_frame_buffer);
		ERR_FAIL_COND_V(base_set.is_null() || !_render_depth(p_targets, p_frame_buffer, p_material_sampler), false);
	}

	RENDER_TIMESTAMP("Yutrel BasePass");
	const YutrelScopedLabel pass_label("Yutrel BasePass", Color(0.2, 0.7, 1.0));
	BitField<RD::DrawFlags> clear = RD::DRAW_CLEAR_COLOR_ALL;
	if (has_draws()) {
		resolve_draws(rd->framebuffer_get_format(p_targets.base_framebuffer), pipelines);
		draws.sort_custom<ByPipelineAndMaterial>();
	} else {
		clear.set_flag(RD::DRAW_CLEAR_DEPTH);
	}
	const Color zero(0, 0, 0, 0);
	const RD::DrawListID list = rd->draw_list_begin(p_targets.base_framebuffer, clear, Vector<Color>{ p_clear_color, zero, zero, zero, zero }, 0.0f, 0, p_targets.region, RDD::BreadcrumbMarker::OPAQUE_PASS);
	if (has_draws()) {
		rd->draw_list_bind_uniform_set(list, base_set, 0);
		BasePassDraw draw;
		draw_range(list, 0, draws.size(), material_sets, draw, p_render_info);
	}
	rd->draw_list_end();
	return true;
}

} // namespace RendererSceneRenderImplementation
