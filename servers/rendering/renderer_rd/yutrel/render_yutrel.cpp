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
	RD::get_singleton()->draw_command_begin_label("Yutrel: Clear viewport", Color(0.15, 0.05, 0.25));
	textures->render_target_request_clear(target, Color(0.4, 0.8, 1.0, 1.0));
	textures->render_target_do_clear_request(target);
	RD::get_singleton()->draw_command_end_label();

	if (!first_frame_rendered) {
		print_verbose("Yutrel: first viewport clear completed.");
		first_frame_rendered = true;
	}
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
	print_verbose("Yutrel: independent scene renderer created (clear-only bootstrap).");
}

RenderYutrel::~RenderYutrel() {
	// This can call base_uniforms_changed(), so release before the base destructor.
	RendererRD::LightStorage::get_singleton()->directional_shadow_atlas_set_size(0);
}

} // namespace RendererSceneRenderImplementation
