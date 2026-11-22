/**************************************************************************/
/*  yutrel_ddgi_volume.h                                                  */
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

#include "scene/3d/node_3d.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_ddgi_settings.h"

class YutrelDDGIVolume : public Node3D {
	GDCLASS(YutrelDDGIVolume, Node3D);

	RendererSceneRenderImplementation::YutrelDDGISettings settings;
	Vector3 center;
	RID scenario;
	void _publish();
	void _update_warnings();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	YutrelDDGIVolume();
	AABB get_world_bounds() const;
	PackedStringArray get_configuration_warnings() const override;
	void set_center(const Vector3 &p_value);
	Vector3 get_center() const { return center; }
	void set_size(const Vector3 &p_value);
	Vector3 get_size() const { return settings.size; }
	void set_probe_counts(const Vector3i &p_value);
	Vector3i get_probe_counts() const { return settings.probe_counts; }
	void set_rays_per_probe(int p_value);
	int get_rays_per_probe() const { return settings.rays_per_probe; }
	void set_max_ray_distance(float p_value);
	float get_max_ray_distance() const { return settings.max_ray_distance; }
	void set_infinite_bounces(bool p_value);
	bool get_infinite_bounces() const { return settings.infinite_bounces; }
	void set_hysteresis(float p_value);
	float get_hysteresis() const { return settings.hysteresis; }
	void set_normal_bias(float p_value);
	float get_normal_bias() const { return settings.normal_bias; }
	void set_view_bias(float p_value);
	float get_view_bias() const { return settings.view_bias; }
	void set_distance_exponent(float p_value);
	float get_distance_exponent() const { return settings.distance_exponent; }
	void set_irradiance_gamma(float p_value);
	float get_irradiance_gamma() const { return settings.irradiance_gamma; }
	void set_irradiance_threshold(float p_value);
	float get_irradiance_threshold() const { return settings.irradiance_threshold; }
	void set_brightness_threshold(float p_value);
	float get_brightness_threshold() const { return settings.brightness_threshold; }
	void set_backface_threshold(float p_value);
	float get_backface_threshold() const { return settings.backface_threshold; }
	void set_intensity(float p_value);
	float get_intensity() const { return settings.intensity; }
	void set_probe_preview_radius(float p_value);
	float get_probe_preview_radius() const { return settings.probe_preview_radius; }
};
