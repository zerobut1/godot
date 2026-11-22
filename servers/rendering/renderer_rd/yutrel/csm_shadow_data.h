/**************************************************************************/
/*  csm_shadow_data.h                                                     */
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

#include "core/typedefs.h"

namespace RendererSceneRenderImplementation {

// float4 rows and packed cascade vectors match shadow_mask.slang.
struct CSMShadowData {
	float view_projection[4][4][4] = {};
	float splits[4] = {};
	float world_texel_size[4] = {};
	float depth_bias[4] = {};
	float normal_bias = 0;
	float blur_radius = 0;
	float opacity = 1;
	float inv_resolution = 0;
	float fade_start = 0;
	float max_distance = 0;
	uint32_t cascade_count = 0;
	uint32_t first_layer = 0;
	uint32_t blend_splits = 0;
	float camera_near = 0;
	float padding[2] = {};
};

static_assert(sizeof(CSMShadowData) == 352);
static_assert(offsetof(CSMShadowData, splits) == 256);
static_assert(offsetof(CSMShadowData, first_layer) == 332);

} // namespace RendererSceneRenderImplementation
