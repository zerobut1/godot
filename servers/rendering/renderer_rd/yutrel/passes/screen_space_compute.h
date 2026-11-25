/**************************************************************************/
/*  screen_space_compute.h                                                */
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

#pragma once

#include "servers/rendering/rendering_device.h"

namespace RendererSceneRenderImplementation::ScreenSpaceCompute {

inline RID create_image(Size2i p_size, RD::DataFormat p_format, const char *p_name) {
	RD::TextureFormat format;
	format.width = p_size.x;
	format.height = p_size.y;
	format.format = p_format;
	format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	RD *rd = RD::get_singleton();
	const RID image = rd->texture_create(format, {});
	if (image.is_valid()) {
		rd->set_resource_name(image, p_name);
	}
	return image;
}

template <typename Params>
void dispatch(Span<char> p_label, RID p_pipeline, RID p_set, const Params &p_params, Size2i p_size, RID p_scene_set = RID(), const Color &p_color = Color(0.5, 0.7, 0.9)) {
	// Separate lists expose each image write/read dependency to RD's command graph.
	RD *rd = RD::get_singleton();
	if (!p_label.is_empty()) {
		rd->draw_command_begin_label(p_label, p_color);
	}
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, p_pipeline);
	rd->compute_list_bind_uniform_set(list, p_set, 0);
	if (p_scene_set.is_valid()) {
		rd->compute_list_bind_uniform_set(list, p_scene_set, 1);
	}
	rd->compute_list_set_push_constant(list, &p_params, sizeof(p_params));
	rd->compute_list_dispatch_threads(list, p_size.x, p_size.y, 1);
	rd->compute_list_end();
	if (!p_label.is_empty()) {
		rd->draw_command_end_label();
	}
}

} // namespace RendererSceneRenderImplementation::ScreenSpaceCompute
