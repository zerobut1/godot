/**************************************************************************/
/*  skybox_pass.cpp                                                       */
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

#include "skybox_pass.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

void SkyboxPass::prepare() {
	if (refresh_shader(shader, "skybox.slang")) {
		RD::PipelineDepthStencilState depth;
		depth.enable_depth_test = true;
		depth.enable_depth_write = false;
		depth.depth_compare_operator = RD::COMPARE_OP_EQUAL;
		auto blend = RD::PipelineColorBlendState::create_disabled(2);
		pipeline.setup(shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, depth, blend);
		auto &color = blend.attachments.write[0];
		color.write_r = color.write_g = color.write_b = color.write_a = false;
		velocity_only.setup(shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, depth, blend);
	}
}

void SkyboxPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_source_cubemap, RID p_cube_sampler, const YutrelSkyParams &p_sky) {
	if (shader.get_shader().is_null()) {
		return;
	}

	const bool draw_color = p_source_cubemap.is_valid();
	if (!draw_color) {
		using Storage = RendererRD::TextureStorage;
		p_source_cubemap = Storage::get_singleton()->texture_rd_get_default(Storage::DEFAULT_RD_TEXTURE_CUBEMAP_BLACK);
		p_cube_sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	}
	const RID framebuffer = FramebufferCacheRD::get_singleton()->get_cache(p_targets.scene_color, p_targets.velocity, p_targets.scene_depth);
	ERR_FAIL_COND(framebuffer.is_null());
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_source_cubemap),
			RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 2, p_cube_sampler));
	draw_fullscreen("Yutrel SkyboxPass", Color(0.4, 0.5, 0.9), p_targets.region, framebuffer, draw_color ? pipeline : velocity_only, set, &p_sky, sizeof(p_sky));
}

} // namespace RendererSceneRenderImplementation
