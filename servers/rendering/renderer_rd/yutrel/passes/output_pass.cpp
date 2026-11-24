/**************************************************************************/
/*  output_pass.cpp                                                       */
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

#include "output_pass.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_environment_settings.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

namespace {

struct OutputPushConstant {
	uint32_t output_linear;
	uint32_t tone_mapping;
	float exposure;
};

static_assert(sizeof(OutputPushConstant) == 12);
static_assert(offsetof(OutputPushConstant, exposure) == 8);

} // namespace

bool OutputPass::prepare() {
	if (refresh_shader(output_shader, "output.slang")) {
		output_pipeline.setup(output_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	return output_shader.get_shader().is_valid();
}

void OutputPass::render(const YutrelPipelineTargets &p_targets, RID p_environment, RID p_color_override) {
	const RID output_set = UniformSetCacheRD::get_singleton()->get_cache(output_shader.get_shader(), 0, RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, p_color_override.is_valid() ? p_color_override : p_targets.scene_color));
	const YutrelEnvironmentSettings environment = YutrelEnvironmentSettings::get(p_environment);
	const OutputPushConstant output = { p_targets.output_linear, uint32_t(environment.tone_mapping), environment.exposure };
	draw_fullscreen("Yutrel OutputPass", Color(0.7, 0.9, 0.2), p_targets.output_region, p_targets.output_framebuffer, output_pipeline, output_set, &output, sizeof(output));
}

} // namespace RendererSceneRenderImplementation
