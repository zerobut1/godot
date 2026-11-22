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

namespace RendererSceneRenderImplementation {

bool BasePass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const Color &p_clear_color) {
	RID base_set;
	if (has_draws()) {
		ERR_FAIL_COND_V(!upload_geometry(), false);
		base_set = UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, material_buffer.get_buffer()));
		ERR_FAIL_COND_V(base_set.is_null(), false);
	}
	_record(p_targets, base_set, p_clear_color);
	return true;
}

void BasePass::_record(const YutrelPipelineTargets &p_targets, RID p_uniform_set, const Color &p_clear_color) {
	RD *rd = RD::get_singleton();
	resolve_draws(rd->framebuffer_get_format(p_targets.base_framebuffer), false);
	rd->draw_command_begin_label("Yutrel BasePass", Color(0.2, 0.7, 1.0));
	const Color zero(0, 0, 0, 0);
	const RD::DrawListID list = rd->draw_list_begin(p_targets.base_framebuffer, RD::DRAW_CLEAR_COLOR_ALL | RD::DRAW_CLEAR_DEPTH, Vector<Color>{ p_clear_color, zero, zero, zero }, 0.0f, 0, p_targets.region, RDD::BreadcrumbMarker::OPAQUE_PASS);
	if (p_uniform_set.is_valid()) {
		rd->draw_list_bind_uniform_set(list, p_uniform_set, 0);
	}
	draw_range(list, 0, draws.size());
	rd->draw_list_end();
	rd->draw_command_end_label();
}

} // namespace RendererSceneRenderImplementation
