/**************************************************************************/
/*  yutrel_environment.cpp                                                */
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

#include "yutrel_environment.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/viewport.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_environment_settings.h"
#include "servers/rendering/rendering_server_default.h"

using RendererSceneRenderImplementation::ToneMappingMode;
using RendererSceneRenderImplementation::YutrelEnvironmentSettings;

static_assert(int(YutrelEnvironment::TONE_MAPPING_AGX) == int(ToneMappingMode::AGX));
static_assert(int(YutrelEnvironment::TONE_MAPPING_AGX_PUNCHY) == int(ToneMappingMode::AGX_PUNCHY));
static_assert(int(YutrelEnvironment::TONE_MAPPING_ACES) == int(ToneMappingMode::ACES));
static_assert(int(YutrelEnvironment::TONE_MAPPING_NONE) == int(ToneMappingMode::NONE));
static_assert(int(YutrelEnvironment::SKY_MODE_CONSTANT_COLOR) == int(RendererSceneRenderImplementation::SkyMode::CONSTANT_COLOR));
static_assert(int(YutrelEnvironment::SKY_MODE_HDRI) == int(RendererSceneRenderImplementation::SkyMode::HDRI));

static_assert(int(YutrelEnvironment::SHADOW_MODE_CSM) == int(RendererSceneRenderImplementation::ShadowMode::CSM));
static_assert(int(YutrelEnvironment::SHADOW_MODE_RAY_TRACED) == int(RendererSceneRenderImplementation::ShadowMode::RAY_TRACED));

void YutrelEnvironment::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_sky_mode", "sky_mode"), &YutrelEnvironment::set_sky_mode);
	ClassDB::bind_method(D_METHOD("get_sky_mode"), &YutrelEnvironment::get_sky_mode);
	ClassDB::bind_method(D_METHOD("set_sky_color", "sky_color"), &YutrelEnvironment::set_sky_color);
	ClassDB::bind_method(D_METHOD("get_sky_color"), &YutrelEnvironment::get_sky_color);
	ClassDB::bind_method(D_METHOD("set_sky_texture", "sky_texture"), &YutrelEnvironment::set_sky_texture);
	ClassDB::bind_method(D_METHOD("get_sky_texture"), &YutrelEnvironment::get_sky_texture);
	ClassDB::bind_method(D_METHOD("set_sky_intensity", "sky_intensity"), &YutrelEnvironment::set_sky_intensity);
	ClassDB::bind_method(D_METHOD("get_sky_intensity"), &YutrelEnvironment::get_sky_intensity);
	ClassDB::bind_method(D_METHOD("set_sky_rotation", "sky_rotation"), &YutrelEnvironment::set_sky_rotation);
	ClassDB::bind_method(D_METHOD("get_sky_rotation"), &YutrelEnvironment::get_sky_rotation);
	ClassDB::bind_method(D_METHOD("set_tone_mapping", "tone_mapping"), &YutrelEnvironment::set_tone_mapping);
	ClassDB::bind_method(D_METHOD("get_tone_mapping"), &YutrelEnvironment::get_tone_mapping);
	ClassDB::bind_method(D_METHOD("set_exposure", "exposure"), &YutrelEnvironment::set_exposure);
	ClassDB::bind_method(D_METHOD("get_exposure"), &YutrelEnvironment::get_exposure);

	BIND_ENUM_CONSTANT(SHADOW_MODE_CSM);
	BIND_ENUM_CONSTANT(SHADOW_MODE_RAY_TRACED);
	BIND_ENUM_CONSTANT(TONE_MAPPING_AGX);
	BIND_ENUM_CONSTANT(TONE_MAPPING_AGX_PUNCHY);
	BIND_ENUM_CONSTANT(TONE_MAPPING_ACES);
	BIND_ENUM_CONSTANT(TONE_MAPPING_NONE);
	BIND_ENUM_CONSTANT(SKY_MODE_CONSTANT_COLOR);
	BIND_ENUM_CONSTANT(SKY_MODE_HDRI);

	ClassDB::bind_method(D_METHOD("set_shadow_mode", "mode"), &YutrelEnvironment::set_shadow_mode);
	ClassDB::bind_method(D_METHOD("get_shadow_mode"), &YutrelEnvironment::get_shadow_mode);
	ClassDB::bind_method(D_METHOD("set_shadow_rt_half_resolution", "enabled"), &YutrelEnvironment::set_shadow_rt_half_resolution);
	ClassDB::bind_method(D_METHOD("get_shadow_rt_half_resolution"), &YutrelEnvironment::get_shadow_rt_half_resolution);
	ClassDB::bind_method(D_METHOD("set_shadow_rt_samples", "samples"), &YutrelEnvironment::set_shadow_rt_samples);
	ClassDB::bind_method(D_METHOD("get_shadow_rt_samples"), &YutrelEnvironment::get_shadow_rt_samples);
	ClassDB::bind_method(D_METHOD("set_shadow_resolution", "resolution"), &YutrelEnvironment::set_shadow_resolution);
	ClassDB::bind_method(D_METHOD("get_shadow_resolution"), &YutrelEnvironment::get_shadow_resolution);
	ClassDB::bind_method(D_METHOD("set_rtao_enabled", "enabled"), &YutrelEnvironment::set_rtao_enabled);
	ClassDB::bind_method(D_METHOD("get_rtao_enabled"), &YutrelEnvironment::get_rtao_enabled);
	ClassDB::bind_method(D_METHOD("set_rtao_half_resolution", "enabled"), &YutrelEnvironment::set_rtao_half_resolution);
	ClassDB::bind_method(D_METHOD("get_rtao_half_resolution"), &YutrelEnvironment::get_rtao_half_resolution);
	ClassDB::bind_method(D_METHOD("set_rtao_radius", "radius"), &YutrelEnvironment::set_rtao_radius);
	ClassDB::bind_method(D_METHOD("get_rtao_radius"), &YutrelEnvironment::get_rtao_radius);
	ClassDB::bind_method(D_METHOD("set_rtao_intensity", "intensity"), &YutrelEnvironment::set_rtao_intensity);
	ClassDB::bind_method(D_METHOD("get_rtao_intensity"), &YutrelEnvironment::get_rtao_intensity);
	ClassDB::bind_method(D_METHOD("set_rtao_samples", "samples"), &YutrelEnvironment::set_rtao_samples);
	ClassDB::bind_method(D_METHOD("get_rtao_samples"), &YutrelEnvironment::get_rtao_samples);

	ClassDB::bind_method(D_METHOD("set_rt_reflection_enabled", "enabled"), &YutrelEnvironment::set_rt_reflection_enabled);
	ClassDB::bind_method(D_METHOD("get_rt_reflection_enabled"), &YutrelEnvironment::get_rt_reflection_enabled);
	ClassDB::bind_method(D_METHOD("set_rt_reflection_half_resolution", "enabled"), &YutrelEnvironment::set_rt_reflection_half_resolution);
	ClassDB::bind_method(D_METHOD("get_rt_reflection_half_resolution"), &YutrelEnvironment::get_rt_reflection_half_resolution);
	ClassDB::bind_method(D_METHOD("set_rt_reflection_max_roughness", "roughness"), &YutrelEnvironment::set_rt_reflection_max_roughness);
	ClassDB::bind_method(D_METHOD("get_rt_reflection_max_roughness"), &YutrelEnvironment::get_rt_reflection_max_roughness);
	ClassDB::bind_method(D_METHOD("set_rt_reflection_samples", "samples"), &YutrelEnvironment::set_rt_reflection_samples);
	ClassDB::bind_method(D_METHOD("get_rt_reflection_samples"), &YutrelEnvironment::get_rt_reflection_samples);

	ADD_GROUP("Shadow", "shadow_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shadow_mode", PROPERTY_HINT_ENUM, "CSM,Ray Traced"), "set_shadow_mode", "get_shadow_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shadow_resolution", PROPERTY_HINT_ENUM, "1024:1024,2048:2048,4096:4096"), "set_shadow_resolution", "get_shadow_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shadow_rt_half_resolution"), "set_shadow_rt_half_resolution", "get_shadow_rt_half_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shadow_rt_samples", PROPERTY_HINT_RANGE, "1,8,1"), "set_shadow_rt_samples", "get_shadow_rt_samples");

	ADD_GROUP("Sky", "sky_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sky_mode", PROPERTY_HINT_ENUM, "Constant Color,HDRI Texture"), "set_sky_mode", "get_sky_mode");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "sky_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_sky_color", "get_sky_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sky_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_sky_texture", "get_sky_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_intensity", PROPERTY_HINT_RANGE, "0.0,16.0,0.05,or_greater"), "set_sky_intensity", "get_sky_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sky_rotation", PROPERTY_HINT_RANGE, "-180.0,180.0,0.5"), "set_sky_rotation", "get_sky_rotation");
	ADD_GROUP("RTAO", "rtao_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rtao_enabled"), "set_rtao_enabled", "get_rtao_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rtao_half_resolution"), "set_rtao_half_resolution", "get_rtao_half_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rtao_radius", PROPERTY_HINT_RANGE, "0.05,10.0,0.05"), "set_rtao_radius", "get_rtao_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rtao_intensity", PROPERTY_HINT_RANGE, "0.0,4.0,0.05"), "set_rtao_intensity", "get_rtao_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rtao_samples", PROPERTY_HINT_RANGE, "1,8,1"), "set_rtao_samples", "get_rtao_samples");
	ADD_GROUP("RT Reflection", "rt_reflection_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rt_reflection_enabled"), "set_rt_reflection_enabled", "get_rt_reflection_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rt_reflection_half_resolution"), "set_rt_reflection_half_resolution", "get_rt_reflection_half_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rt_reflection_max_roughness", PROPERTY_HINT_RANGE, "0.1,1.0,0.05"), "set_rt_reflection_max_roughness", "get_rt_reflection_max_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rt_reflection_samples", PROPERTY_HINT_RANGE, "1,4,1"), "set_rt_reflection_samples", "get_rt_reflection_samples");
	ADD_GROUP("Tone Mapping", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tone_mapping", PROPERTY_HINT_ENUM, "AgX,AgX Punchy,ACES,None"), "set_tone_mapping", "get_tone_mapping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure", PROPERTY_HINT_RANGE, "0.0,16.0,0.05,or_greater"), "set_exposure", "get_exposure");
}

void YutrelEnvironment::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Viewport *viewport = get_viewport();
			if (!viewport) {
				return;
			}
			world = viewport->find_world_3d();
			if (world.is_null()) {
				return;
			}
			if (internal_environment.is_null()) {
				internal_environment.instantiate();
			}
			_publish_settings();
			world->connect_changed(callable_mp(this, &YutrelEnvironment::_world_changed));
			_world_changed();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (world.is_null()) {
				return;
			}
			world->disconnect_changed(callable_mp(this, &YutrelEnvironment::_world_changed));
			YutrelEnvironmentSettings::erase(internal_environment->get_rid());
			if (world->get_environment() == internal_environment) {
				world->set_environment(previous_environment);
			}
			previous_environment.unref();
			world.unref();
		} break;
	}
}

void YutrelEnvironment::set_shadow_mode(ShadowMode p_mode) {
	ERR_FAIL_COND(p_mode != SHADOW_MODE_CSM && p_mode != SHADOW_MODE_RAY_TRACED);
	shadow_mode = p_mode;
	_publish_settings();
}

void YutrelEnvironment::set_shadow_rt_half_resolution(bool p_enabled) {
	shadow_rt_half_resolution = p_enabled;
	_publish_settings();
}

void YutrelEnvironment::set_shadow_rt_samples(int p_samples) {
	shadow_rt_samples = CLAMP(p_samples, 1, 8);
	_publish_settings();
}

void YutrelEnvironment::set_shadow_resolution(int p_resolution) {
	ERR_FAIL_COND(p_resolution != 1024 && p_resolution != 2048 && p_resolution != 4096);
	shadow_resolution = p_resolution;
	_publish_settings();
}

void YutrelEnvironment::set_sky_mode(SkyMode p_mode) {
	ERR_FAIL_COND(p_mode != SKY_MODE_CONSTANT_COLOR && p_mode != SKY_MODE_HDRI);
	sky_mode = p_mode;
	_publish_settings();
}

void YutrelEnvironment::set_sky_color(const Color &p_color) {
	ERR_FAIL_COND(!Math::is_finite(p_color.r) || !Math::is_finite(p_color.g) || !Math::is_finite(p_color.b) || !Math::is_finite(p_color.a));
	sky_color = p_color;
	_publish_settings();
}

void YutrelEnvironment::set_sky_texture(const Ref<Texture2D> &p_texture) {
	sky_texture = p_texture;
	_publish_settings();
}

void YutrelEnvironment::set_sky_intensity(float p_intensity) {
	ERR_FAIL_COND(!Math::is_finite(p_intensity) || p_intensity < 0.0f);
	sky_intensity = p_intensity;
	_publish_settings();
}

void YutrelEnvironment::set_sky_rotation(float p_degrees) {
	ERR_FAIL_COND(!Math::is_finite(p_degrees));
	sky_rotation = p_degrees;
	_publish_settings();
}

void YutrelEnvironment::set_tone_mapping(ToneMapping p_tone_mapping) {
	ERR_FAIL_COND(p_tone_mapping < TONE_MAPPING_AGX || p_tone_mapping > TONE_MAPPING_NONE);
	tone_mapping = p_tone_mapping;
	_publish_settings();
}

void YutrelEnvironment::set_exposure(float p_exposure) {
	ERR_FAIL_COND(!Math::is_finite(p_exposure) || p_exposure < 0.0f);
	exposure = p_exposure;
	_publish_settings();
}

void YutrelEnvironment::set_rtao_enabled(bool p_enabled) {
	rtao_enabled = p_enabled;
	_publish_settings();
}

void YutrelEnvironment::set_rtao_half_resolution(bool p_enabled) {
	rtao_half_resolution = p_enabled;
	_publish_settings();
}

void YutrelEnvironment::set_rtao_radius(float p_radius) {
	ERR_FAIL_COND(!Math::is_finite(p_radius));
	rtao_radius = CLAMP(p_radius, 0.05f, 10.0f);
	_publish_settings();
}

void YutrelEnvironment::set_rtao_intensity(float p_intensity) {
	ERR_FAIL_COND(!Math::is_finite(p_intensity));
	rtao_intensity = CLAMP(p_intensity, 0.0f, 4.0f);
	_publish_settings();
}

void YutrelEnvironment::set_rtao_samples(int p_samples) {
	rtao_samples = CLAMP(p_samples, 1, 8);
	_publish_settings();
}

void YutrelEnvironment::set_rt_reflection_enabled(bool p_enabled) {
	rt_reflection_enabled = p_enabled;
	_publish_settings();
}

void YutrelEnvironment::set_rt_reflection_half_resolution(bool p_enabled) {
	rt_reflection_half_resolution = p_enabled;
	_publish_settings();
}

void YutrelEnvironment::set_rt_reflection_max_roughness(float p_roughness) {
	ERR_FAIL_COND(!Math::is_finite(p_roughness));
	rt_reflection_max_roughness = CLAMP(p_roughness, 0.1f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::set_rt_reflection_samples(int p_samples) {
	rt_reflection_samples = CLAMP(p_samples, 1, 4);
	_publish_settings();
}

void YutrelEnvironment::_publish_settings() {
	if (world.is_valid()) {
		const YutrelEnvironmentSettings settings = {
			ToneMappingMode(tone_mapping),
			exposure,
			RendererSceneRenderImplementation::SkyMode(sky_mode),
			sky_color.srgb_to_linear(),
			sky_texture.is_valid() ? sky_texture->get_rid() : RID(),
			sky_intensity,
			Math::deg_to_rad(sky_rotation),
			RendererSceneRenderImplementation::ShadowMode(shadow_mode),
			shadow_resolution,
			shadow_rt_half_resolution,
			shadow_rt_samples,
			rtao_enabled,
			rtao_half_resolution,
			rtao_radius,
			rtao_intensity,
			rtao_samples,
			rt_reflection_enabled,
			rt_reflection_half_resolution,
			rt_reflection_max_roughness,
			rt_reflection_samples,
		};
		YutrelEnvironmentSettings::set(internal_environment->get_rid(), settings);
		RenderingServerDefault::redraw_request();
	}
}

void YutrelEnvironment::_world_changed() {
	const Ref<Environment> current = world->get_environment();
	if (current.is_null() || !YutrelEnvironmentSettings::has(current->get_rid())) {
		previous_environment = current;
		world->set_environment(internal_environment);
	}
	update_configuration_warnings();
}

PackedStringArray YutrelEnvironment::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (world.is_valid() && world->get_environment() != internal_environment) {
		warnings.push_back(RTR("Another environment is active in this World3D. Only one environment can be active in each world."));
	}
	return warnings;
}
