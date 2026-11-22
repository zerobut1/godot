/**************************************************************************/
/*  yutrel_ddgi_volume.cpp                                                */
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

#include "yutrel_ddgi_volume.h"

#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server_default.h"

using RendererSceneRenderImplementation::YutrelDDGISettings;

YutrelDDGIVolume::YutrelDDGIVolume() {
	set_notify_transform(true);
}

AABB YutrelDDGIVolume::get_world_bounds() const {
	const Transform3D transform = get_global_transform();
	const Vector3 world_size = settings.size * transform.basis.get_scale_abs();
	return AABB(transform.origin + center - world_size * 0.5f, world_size);
}

void YutrelDDGIVolume::_update_warnings() {
#ifdef TOOLS_ENABLED
	get_tree()->call_group_flags(SceneTree::GROUP_CALL_DEFERRED | SceneTree::GROUP_CALL_UNIQUE, SNAME("_yutrel_ddgi_volumes"), SNAME("update_configuration_warnings"));
#endif
}

void YutrelDDGIVolume::_publish() {
	if (scenario.is_null()) {
		return;
	}
	bool membership_changed;
	if (is_visible_in_tree()) {
		YutrelDDGISettings snapshot = settings;
		const AABB bounds = get_world_bounds();
		snapshot.origin = get_global_position() + center;
		snapshot.size = bounds.size;
		snapshot.owner = get_instance_id();
		membership_changed = YutrelDDGISettings::publish(scenario, snapshot);
	} else {
		membership_changed = YutrelDDGISettings::erase(scenario, get_instance_id());
	}
	if (membership_changed) {
		_update_warnings();
	}
	update_gizmos();
	RenderingServerDefault::redraw_request();
}

void YutrelDDGIVolume::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			scenario = get_world_3d()->get_scenario();
#ifdef TOOLS_ENABLED
			add_to_group(SNAME("_yutrel_ddgi_volumes"));
#endif
			_publish();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED:
		case NOTIFICATION_VISIBILITY_CHANGED: {
			_publish();
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			YutrelDDGISettings::erase(scenario, get_instance_id());
			scenario = RID();
#ifdef TOOLS_ENABLED
			remove_from_group(SNAME("_yutrel_ddgi_volumes"));
#endif
			_update_warnings();
			RenderingServerDefault::redraw_request();
		} break;
	}
}

PackedStringArray YutrelDDGIVolume::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (is_visible_in_tree() && YutrelDDGISettings::has_multiple(scenario)) {
		warnings.push_back(RTR("Multiple visible DDGI volumes share this World3D. Only the first visible volume is active."));
	}
	return warnings;
}

void YutrelDDGIVolume::set_center(const Vector3 &p_value) {
	ERR_FAIL_COND(!p_value.is_finite());
	center = p_value;
	_publish();
}

void YutrelDDGIVolume::set_size(const Vector3 &p_value) {
	ERR_FAIL_COND(!p_value.is_finite());
	settings.size = p_value.maxf(0.01f);
	_publish();
}

void YutrelDDGIVolume::set_probe_counts(const Vector3i &p_value) {
	settings.probe_counts = p_value.clampi(2, 64);
	_publish();
}

void YutrelDDGIVolume::set_rays_per_probe(int p_value) {
	settings.rays_per_probe = CLAMP(p_value, 16, 1024);
	_publish();
}

void YutrelDDGIVolume::set_max_ray_distance(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.max_ray_distance = MAX(p_value, 0.1f);
	_publish();
}

void YutrelDDGIVolume::set_infinite_bounces(bool p_value) {
	settings.infinite_bounces = p_value;
	_publish();
}

void YutrelDDGIVolume::set_hysteresis(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.hysteresis = CLAMP(p_value, 0.0f, 0.99f);
	_publish();
}

void YutrelDDGIVolume::set_normal_bias(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.normal_bias = CLAMP(p_value, 0.0f, 2.0f);
	_publish();
}

void YutrelDDGIVolume::set_view_bias(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.view_bias = CLAMP(p_value, 0.0f, 2.0f);
	_publish();
}

void YutrelDDGIVolume::set_distance_exponent(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.distance_exponent = CLAMP(p_value, 1.0f, 100.0f);
	_publish();
}

void YutrelDDGIVolume::set_irradiance_gamma(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.irradiance_gamma = CLAMP(p_value, 1.0f, 10.0f);
	_publish();
}

void YutrelDDGIVolume::set_irradiance_threshold(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.irradiance_threshold = MAX(p_value, 0.0f);
	_publish();
}

void YutrelDDGIVolume::set_brightness_threshold(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.brightness_threshold = MAX(p_value, 0.0f);
	_publish();
}

void YutrelDDGIVolume::set_backface_threshold(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.backface_threshold = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelDDGIVolume::set_intensity(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.intensity = CLAMP(p_value, 0.0f, 16.0f);
	_publish();
}

void YutrelDDGIVolume::set_probe_relocation(bool p_value) {
	settings.probe_relocation = p_value;
	_publish();
}

void YutrelDDGIVolume::set_probe_classification(bool p_value) {
	settings.probe_classification = p_value;
	_publish();
}

void YutrelDDGIVolume::set_min_frontface_distance(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.min_frontface_distance = CLAMP(p_value, 0.01f, 2.0f);
	_publish();
}

void YutrelDDGIVolume::set_fixed_ray_backface_threshold(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.fixed_ray_backface_threshold = CLAMP(p_value, 0.0f, 1.0f);
	_publish();
}

void YutrelDDGIVolume::set_probe_preview_radius(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	settings.probe_preview_radius = MAX(p_value, 0.01f);
	_publish();
}

void YutrelDDGIVolume::_bind_methods() {
	ADD_GROUP("Bounds", "");
	ClassDB::bind_method(D_METHOD("set_center", "value"), &YutrelDDGIVolume::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &YutrelDDGIVolume::get_center);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
	ClassDB::bind_method(D_METHOD("set_size", "value"), &YutrelDDGIVolume::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &YutrelDDGIVolume::get_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_RANGE, "0.01,100,0.01,or_greater"), "set_size", "get_size");
	ClassDB::bind_method(D_METHOD("set_probe_counts", "value"), &YutrelDDGIVolume::set_probe_counts);
	ClassDB::bind_method(D_METHOD("get_probe_counts"), &YutrelDDGIVolume::get_probe_counts);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "probe_counts", PROPERTY_HINT_RANGE, "2,64,1"), "set_probe_counts", "get_probe_counts");

	ADD_GROUP("Trace", "");
	ClassDB::bind_method(D_METHOD("set_rays_per_probe", "value"), &YutrelDDGIVolume::set_rays_per_probe);
	ClassDB::bind_method(D_METHOD("get_rays_per_probe"), &YutrelDDGIVolume::get_rays_per_probe);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rays_per_probe", PROPERTY_HINT_RANGE, "16,1024,1"), "set_rays_per_probe", "get_rays_per_probe");
	ClassDB::bind_method(D_METHOD("set_max_ray_distance", "value"), &YutrelDDGIVolume::set_max_ray_distance);
	ClassDB::bind_method(D_METHOD("get_max_ray_distance"), &YutrelDDGIVolume::get_max_ray_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_ray_distance", PROPERTY_HINT_RANGE, "0.1,100,0.1,or_greater"), "set_max_ray_distance", "get_max_ray_distance");
	ClassDB::bind_method(D_METHOD("set_infinite_bounces", "value"), &YutrelDDGIVolume::set_infinite_bounces);
	ClassDB::bind_method(D_METHOD("get_infinite_bounces"), &YutrelDDGIVolume::get_infinite_bounces);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "infinite_bounces"), "set_infinite_bounces", "get_infinite_bounces");

	ADD_GROUP("Blending & Sampling", "");
	ClassDB::bind_method(D_METHOD("set_hysteresis", "value"), &YutrelDDGIVolume::set_hysteresis);
	ClassDB::bind_method(D_METHOD("get_hysteresis"), &YutrelDDGIVolume::get_hysteresis);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hysteresis", PROPERTY_HINT_RANGE, "0,0.99,0.001"), "set_hysteresis", "get_hysteresis");
	ClassDB::bind_method(D_METHOD("set_normal_bias", "value"), &YutrelDDGIVolume::set_normal_bias);
	ClassDB::bind_method(D_METHOD("get_normal_bias"), &YutrelDDGIVolume::get_normal_bias);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_bias", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_normal_bias", "get_normal_bias");
	ClassDB::bind_method(D_METHOD("set_view_bias", "value"), &YutrelDDGIVolume::set_view_bias);
	ClassDB::bind_method(D_METHOD("get_view_bias"), &YutrelDDGIVolume::get_view_bias);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_bias", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_view_bias", "get_view_bias");
	ClassDB::bind_method(D_METHOD("set_distance_exponent", "value"), &YutrelDDGIVolume::set_distance_exponent);
	ClassDB::bind_method(D_METHOD("get_distance_exponent"), &YutrelDDGIVolume::get_distance_exponent);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distance_exponent", PROPERTY_HINT_RANGE, "1,100,0.01"), "set_distance_exponent", "get_distance_exponent");
	ClassDB::bind_method(D_METHOD("set_irradiance_gamma", "value"), &YutrelDDGIVolume::set_irradiance_gamma);
	ClassDB::bind_method(D_METHOD("get_irradiance_gamma"), &YutrelDDGIVolume::get_irradiance_gamma);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "irradiance_gamma", PROPERTY_HINT_RANGE, "1,10,0.01"), "set_irradiance_gamma", "get_irradiance_gamma");
	ClassDB::bind_method(D_METHOD("set_irradiance_threshold", "value"), &YutrelDDGIVolume::set_irradiance_threshold);
	ClassDB::bind_method(D_METHOD("get_irradiance_threshold"), &YutrelDDGIVolume::get_irradiance_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "irradiance_threshold", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_irradiance_threshold", "get_irradiance_threshold");
	ClassDB::bind_method(D_METHOD("set_brightness_threshold", "value"), &YutrelDDGIVolume::set_brightness_threshold);
	ClassDB::bind_method(D_METHOD("get_brightness_threshold"), &YutrelDDGIVolume::get_brightness_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brightness_threshold", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_brightness_threshold", "get_brightness_threshold");
	ClassDB::bind_method(D_METHOD("set_backface_threshold", "value"), &YutrelDDGIVolume::set_backface_threshold);
	ClassDB::bind_method(D_METHOD("get_backface_threshold"), &YutrelDDGIVolume::get_backface_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "backface_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_backface_threshold", "get_backface_threshold");
	ClassDB::bind_method(D_METHOD("set_intensity", "value"), &YutrelDDGIVolume::set_intensity);
	ClassDB::bind_method(D_METHOD("get_intensity"), &YutrelDDGIVolume::get_intensity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_intensity", "get_intensity");

	ADD_GROUP("Relocation & Classification", "");
	ClassDB::bind_method(D_METHOD("set_probe_relocation", "value"), &YutrelDDGIVolume::set_probe_relocation);
	ClassDB::bind_method(D_METHOD("get_probe_relocation"), &YutrelDDGIVolume::get_probe_relocation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "probe_relocation"), "set_probe_relocation", "get_probe_relocation");
	ClassDB::bind_method(D_METHOD("set_probe_classification", "value"), &YutrelDDGIVolume::set_probe_classification);
	ClassDB::bind_method(D_METHOD("get_probe_classification"), &YutrelDDGIVolume::get_probe_classification);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "probe_classification"), "set_probe_classification", "get_probe_classification");
	ClassDB::bind_method(D_METHOD("set_min_frontface_distance", "value"), &YutrelDDGIVolume::set_min_frontface_distance);
	ClassDB::bind_method(D_METHOD("get_min_frontface_distance"), &YutrelDDGIVolume::get_min_frontface_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_frontface_distance", PROPERTY_HINT_RANGE, "0.01,2,0.01"), "set_min_frontface_distance", "get_min_frontface_distance");
	ClassDB::bind_method(D_METHOD("set_fixed_ray_backface_threshold", "value"), &YutrelDDGIVolume::set_fixed_ray_backface_threshold);
	ClassDB::bind_method(D_METHOD("get_fixed_ray_backface_threshold"), &YutrelDDGIVolume::get_fixed_ray_backface_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fixed_ray_backface_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fixed_ray_backface_threshold", "get_fixed_ray_backface_threshold");

	ADD_GROUP("Debug", "");
	ClassDB::bind_method(D_METHOD("set_probe_preview_radius", "value"), &YutrelDDGIVolume::set_probe_preview_radius);
	ClassDB::bind_method(D_METHOD("get_probe_preview_radius"), &YutrelDDGIVolume::get_probe_preview_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_preview_radius", PROPERTY_HINT_RANGE, "0.01,1,0.01,or_greater"), "set_probe_preview_radius", "get_probe_preview_radius");
}
