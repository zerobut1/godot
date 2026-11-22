/**************************************************************************/
/*  yutrel_ddgi_settings.h                                                */
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

#include "core/math/vector3.h"
#include "core/math/vector3i.h"
#include "core/object/object_id.h"
#include "core/templates/rid.h"

namespace RendererSceneRenderImplementation {

struct YutrelDDGISettings {
	Vector3 origin = Vector3();
	Vector3 size = Vector3(10, 5, 10);
	Vector3i probe_counts = Vector3i(8, 4, 8);
	int rays_per_probe = 256;
	float max_ray_distance = 100.0f;
	bool infinite_bounces = true;
	float hysteresis = 0.97f;
	float normal_bias = 0.1f;
	float view_bias = 0.25f;
	float distance_exponent = 50.0f;
	float irradiance_gamma = 5.0f;
	float irradiance_threshold = 0.25f;
	float brightness_threshold = 0.1f;
	float backface_threshold = 0.1f;
	float intensity = 1.0f;
	bool probe_relocation = true;
	bool probe_classification = true;
	float min_frontface_distance = 0.2f;
	float fixed_ray_backface_threshold = 0.25f;
	float probe_preview_radius = 0.12f;

	uint64_t config_version = 0;
	ObjectID owner;

	static bool get(RID p_scenario, YutrelDDGISettings &r_active);
	static bool publish(RID p_scenario, YutrelDDGISettings p_settings);
	static bool erase(RID p_scenario, ObjectID p_owner);
	static bool has_multiple(RID p_scenario);
};

} // namespace RendererSceneRenderImplementation
