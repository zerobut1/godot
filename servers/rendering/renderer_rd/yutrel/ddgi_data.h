/**************************************************************************/
/*  ddgi_data.h                                                           */
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

#include "core/templates/rid.h"

namespace RendererSceneRenderImplementation {

// Matches ddgi_common.slang's constant buffer layout.
struct alignas(16) DDGIVolumeData {
	float origin[3];
	uint32_t rays_per_probe;
	float spacing[3];
	float max_ray_distance;
	int32_t probe_counts[3];
	float hysteresis;
	float ray_rotation[4];
	float normal_bias, view_bias, distance_exponent, irradiance_gamma;
	float irradiance_threshold, brightness_threshold, backface_threshold, intensity;
	uint32_t infinite_bounces, reset_history;
	float probe_preview_radius;
	uint32_t active;
};
static_assert(sizeof(DDGIVolumeData) == 112);
static_assert(alignof(DDGIVolumeData) == 16);
static_assert(offsetof(DDGIVolumeData, spacing) == 16);
static_assert(offsetof(DDGIVolumeData, probe_counts) == 32);
static_assert(offsetof(DDGIVolumeData, ray_rotation) == 48);
static_assert(offsetof(DDGIVolumeData, normal_bias) == 64);
static_assert(offsetof(DDGIVolumeData, irradiance_threshold) == 80);
static_assert(offsetof(DDGIVolumeData, infinite_bounces) == 96);

struct DDGIBindings {
	RID volume;
	RID irradiance;
	RID distance;
};

} // namespace RendererSceneRenderImplementation
