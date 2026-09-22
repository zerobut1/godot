/**************************************************************************/
/*  render_yutrel.cpp                                                     */
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

#include "render_yutrel.h"

#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/utilities.h"

namespace RendererSceneRenderImplementation {

void RenderYutrel::_render_scene(RenderDataRD *p_render_data, const Color &p_default_color) {
	ERR_FAIL_COND(p_render_data->render_buffers.is_null());
	// Reflection probe baking is outside the bootstrap renderer's supported set.
	if (p_render_data->reflection_probe.is_valid()) {
		return;
	}
	RID target = p_render_data->render_buffers->get_render_target();
	ERR_FAIL_COND(target.is_null());

	RendererRD::TextureStorage *textures = RendererRD::TextureStorage::get_singleton();
	RID framebuffer = textures->render_target_get_rd_framebuffer(target);
	ERR_FAIL_COND(framebuffer.is_null());

	RD *rd = RD::get_singleton();
	ERR_FAIL_NULL(rd);
	if (triangle_shader_version.is_null()) {
		triangle_shader.initialize({ "" });
		triangle_shader_version = triangle_shader.version_create();
		ERR_FAIL_COND_MSG(triangle_shader_version.is_null(), "Yutrel: failed to create the HLSL triangle shader version.");
	}

	const RD::TextureSamples sample_count = RenderSceneBuffersRD::msaa_to_samples(textures->render_target_get_msaa(target));
	const RD::FramebufferFormatID framebuffer_format = rd->framebuffer_get_format(framebuffer);
	if (triangle_pipeline.is_null() || triangle_framebuffer_format != framebuffer_format || triangle_sample_count != sample_count) {
		if (triangle_pipeline.is_valid()) {
			rd->free_rid(triangle_pipeline);
			triangle_pipeline = RID();
		}

		RID triangle_shader_rid = triangle_shader.version_get_shader(triangle_shader_version, 0);
		ERR_FAIL_COND_MSG(triangle_shader_rid.is_null(), "Yutrel: ShaderRD failed to compile or create the HLSL triangle shader.");

		RD::PipelineMultisampleState multisample_state;
		multisample_state.sample_count = sample_count;
		triangle_pipeline = rd->render_pipeline_create(
				triangle_shader_rid,
				framebuffer_format,
				RD::INVALID_FORMAT_ID,
				RD::RENDER_PRIMITIVE_TRIANGLES,
				RD::PipelineRasterizationState(),
				multisample_state,
				RD::PipelineDepthStencilState(),
				RD::PipelineColorBlendState::create_disabled(),
				0);
		ERR_FAIL_COND_MSG(triangle_pipeline.is_null(), "Yutrel: failed to create the triangle render pipeline.");

		triangle_framebuffer_format = framebuffer_format;
		triangle_sample_count = sample_count;
		rd->set_resource_name(triangle_shader_rid, "Yutrel Triangle Shader");
		rd->set_resource_name(triangle_pipeline, "Yutrel Triangle Pipeline");
		print_verbose("Yutrel: HLSL triangle pipeline created through ShaderRD.");
	}

	Color clear_color = Color(0.4, 0.8, 1.0, 1.0);
	if (textures->render_target_is_using_hdr(target)) {
		clear_color = clear_color.srgb_to_linear();
	}
	Vector<Color> clear_colors;
	clear_colors.push_back(clear_color);
	textures->render_target_disable_clear_request(target);
	textures->render_target_set_msaa_needs_resolve(target, false);

	rd->draw_command_begin_label("Yutrel: HLSL triangle", Color(0.2, 0.7, 1.0));
	RD::DrawListID draw_list = rd->draw_list_begin(
			framebuffer,
			RD::DRAW_CLEAR_COLOR_0,
			clear_colors,
			1.0f,
			0,
			p_render_data->render_region,
			RDD::BreadcrumbMarker::OPAQUE_PASS);
	rd->draw_list_bind_render_pipeline(draw_list, triangle_pipeline);
	rd->draw_list_draw(draw_list, false, 1, 3);
	rd->draw_list_end();
	rd->draw_command_end_label();
}

RenderGeometryInstance *RenderYutrel::geometry_instance_create(RID p_base) {
	RSE::InstanceType type = RendererRD::Utilities::get_singleton()->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RSE::INSTANCE_GEOMETRY_MASK), nullptr);
	// Keep the cull/instance lifecycle valid even though geometry is not drawn yet.
	return memnew(GeometryInstanceYutrel(p_base, type));
}

void RenderYutrel::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	ERR_FAIL_NULL(p_geometry_instance);
	memdelete(static_cast<GeometryInstanceYutrel *>(p_geometry_instance));
}

RenderYutrel::RenderYutrel() {
	sky.set_texture_format(_render_buffers_get_preferred_color_format());
	print_verbose("Yutrel: independent scene renderer created (HLSL triangle bootstrap).");
}

RenderYutrel::~RenderYutrel() {
	if (triangle_pipeline.is_valid()) {
		RD::get_singleton()->free_rid(triangle_pipeline);
	}
	if (triangle_shader_version.is_valid()) {
		triangle_shader.version_free(triangle_shader_version);
	}

	// This can call base_uniforms_changed(), so release before the base destructor.
	RendererRD::LightStorage::get_singleton()->directional_shadow_atlas_set_size(0);
}

} // namespace RendererSceneRenderImplementation
