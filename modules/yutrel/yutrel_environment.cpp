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

static_assert(int(YutrelEnvironment::UPSCALER_MODE_OFF) == int(RendererSceneRenderImplementation::UpscalerMode::OFF));
static_assert(int(YutrelEnvironment::UPSCALER_MODE_FSR2) == int(RendererSceneRenderImplementation::UpscalerMode::FSR2));
static_assert(int(YutrelEnvironment::UPSCALER_QUALITY_NATIVE_AA) == int(RendererSceneRenderImplementation::UpscalerQuality::NATIVE_AA));
static_assert(int(YutrelEnvironment::UPSCALER_QUALITY_QUALITY) == int(RendererSceneRenderImplementation::UpscalerQuality::QUALITY));
static_assert(int(YutrelEnvironment::UPSCALER_QUALITY_BALANCED) == int(RendererSceneRenderImplementation::UpscalerQuality::BALANCED));
static_assert(int(YutrelEnvironment::UPSCALER_QUALITY_PERFORMANCE) == int(RendererSceneRenderImplementation::UpscalerQuality::PERFORMANCE));

void YutrelEnvironment::_bind_methods() {
	BIND_ENUM_CONSTANT(UPSCALER_MODE_OFF);
	BIND_ENUM_CONSTANT(UPSCALER_MODE_FSR2);
	BIND_ENUM_CONSTANT(UPSCALER_QUALITY_NATIVE_AA);
	BIND_ENUM_CONSTANT(UPSCALER_QUALITY_QUALITY);
	BIND_ENUM_CONSTANT(UPSCALER_QUALITY_BALANCED);
	BIND_ENUM_CONSTANT(UPSCALER_QUALITY_PERFORMANCE);
	ClassDB::bind_method(D_METHOD("set_upscaler_mode", "value"), &YutrelEnvironment::set_upscaler_mode);
	ClassDB::bind_method(D_METHOD("get_upscaler_mode"), &YutrelEnvironment::get_upscaler_mode);
	ClassDB::bind_method(D_METHOD("set_upscaler_quality", "value"), &YutrelEnvironment::set_upscaler_quality);
	ClassDB::bind_method(D_METHOD("get_upscaler_quality"), &YutrelEnvironment::get_upscaler_quality);
	ClassDB::bind_method(D_METHOD("set_upscaler_sharpness", "value"), &YutrelEnvironment::set_upscaler_sharpness);
	ClassDB::bind_method(D_METHOD("get_upscaler_sharpness"), &YutrelEnvironment::get_upscaler_sharpness);

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
	ADD_GROUP("Upscaler", "upscaler_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "upscaler_mode", PROPERTY_HINT_ENUM, "Off,FSR 2"), "set_upscaler_mode", "get_upscaler_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "upscaler_quality", PROPERTY_HINT_ENUM, "Native AA,Quality,Balanced,Performance"), "set_upscaler_quality", "get_upscaler_quality");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "upscaler_sharpness", PROPERTY_HINT_RANGE, "0.0,1.0,0.05"), "set_upscaler_sharpness", "get_upscaler_sharpness");
	ADD_GROUP("Tone Mapping", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tone_mapping", PROPERTY_HINT_ENUM, "AgX,AgX Punchy,ACES,None"), "set_tone_mapping", "get_tone_mapping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure", PROPERTY_HINT_RANGE, "0.0,16.0,0.05,or_greater"), "set_exposure", "get_exposure");
	ADD_GROUP("Post Process", "");
	ClassDB::bind_method(D_METHOD("set_vignette_intensity", "value"), &YutrelEnvironment::set_vignette_intensity);
	ClassDB::bind_method(D_METHOD("get_vignette_intensity"), &YutrelEnvironment::get_vignette_intensity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vignette_intensity", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_vignette_intensity", "get_vignette_intensity");
	ClassDB::bind_method(D_METHOD("set_vignette_smoothness", "value"), &YutrelEnvironment::set_vignette_smoothness);
	ClassDB::bind_method(D_METHOD("get_vignette_smoothness"), &YutrelEnvironment::get_vignette_smoothness);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vignette_smoothness", PROPERTY_HINT_RANGE, "0.05,1.0,0.01"), "set_vignette_smoothness", "get_vignette_smoothness");
	ClassDB::bind_method(D_METHOD("set_debanding_enabled", "value"), &YutrelEnvironment::set_debanding_enabled);
	ClassDB::bind_method(D_METHOD("get_debanding_enabled"), &YutrelEnvironment::get_debanding_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debanding_enabled"), "set_debanding_enabled", "get_debanding_enabled");

	ADD_GROUP("Auto Exposure", "auto_exposure_");
	ClassDB::bind_method(D_METHOD("set_auto_exposure_enabled", "value"), &YutrelEnvironment::set_auto_exposure_enabled);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_enabled"), &YutrelEnvironment::get_auto_exposure_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_exposure_enabled"), "set_auto_exposure_enabled", "get_auto_exposure_enabled");
	ClassDB::bind_method(D_METHOD("set_auto_exposure_min_ev", "value"), &YutrelEnvironment::set_auto_exposure_min_ev);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_min_ev"), &YutrelEnvironment::get_auto_exposure_min_ev);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_min_ev", PROPERTY_HINT_RANGE, "-12.0,12.0,0.1"), "set_auto_exposure_min_ev", "get_auto_exposure_min_ev");
	ClassDB::bind_method(D_METHOD("set_auto_exposure_max_ev", "value"), &YutrelEnvironment::set_auto_exposure_max_ev);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_max_ev"), &YutrelEnvironment::get_auto_exposure_max_ev);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_max_ev", PROPERTY_HINT_RANGE, "-12.0,12.0,0.1"), "set_auto_exposure_max_ev", "get_auto_exposure_max_ev");
	ClassDB::bind_method(D_METHOD("set_auto_exposure_compensation", "value"), &YutrelEnvironment::set_auto_exposure_compensation);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_compensation"), &YutrelEnvironment::get_auto_exposure_compensation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_compensation", PROPERTY_HINT_RANGE, "-8.0,8.0,0.05"), "set_auto_exposure_compensation", "get_auto_exposure_compensation");
	ClassDB::bind_method(D_METHOD("set_auto_exposure_speed_up", "value"), &YutrelEnvironment::set_auto_exposure_speed_up);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_speed_up"), &YutrelEnvironment::get_auto_exposure_speed_up);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_speed_up", PROPERTY_HINT_RANGE, "0.05,20.0,0.05"), "set_auto_exposure_speed_up", "get_auto_exposure_speed_up");
	ClassDB::bind_method(D_METHOD("set_auto_exposure_speed_down", "value"), &YutrelEnvironment::set_auto_exposure_speed_down);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_speed_down"), &YutrelEnvironment::get_auto_exposure_speed_down);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_speed_down", PROPERTY_HINT_RANGE, "0.05,20.0,0.05"), "set_auto_exposure_speed_down", "get_auto_exposure_speed_down");

	ADD_GROUP("Bloom", "bloom_");
	ClassDB::bind_method(D_METHOD("set_bloom_enabled", "value"), &YutrelEnvironment::set_bloom_enabled);
	ClassDB::bind_method(D_METHOD("get_bloom_enabled"), &YutrelEnvironment::get_bloom_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bloom_enabled"), "set_bloom_enabled", "get_bloom_enabled");
	ClassDB::bind_method(D_METHOD("set_bloom_intensity", "value"), &YutrelEnvironment::set_bloom_intensity);
	ClassDB::bind_method(D_METHOD("get_bloom_intensity"), &YutrelEnvironment::get_bloom_intensity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bloom_intensity", PROPERTY_HINT_RANGE, "0.0,2.0,0.01,or_greater"), "set_bloom_intensity", "get_bloom_intensity");
	ClassDB::bind_method(D_METHOD("set_bloom_threshold", "value"), &YutrelEnvironment::set_bloom_threshold);
	ClassDB::bind_method(D_METHOD("get_bloom_threshold"), &YutrelEnvironment::get_bloom_threshold);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bloom_threshold", PROPERTY_HINT_RANGE, "0.0,16.0,0.05,or_greater"), "set_bloom_threshold", "get_bloom_threshold");
	ClassDB::bind_method(D_METHOD("set_bloom_soft_knee", "value"), &YutrelEnvironment::set_bloom_soft_knee);
	ClassDB::bind_method(D_METHOD("get_bloom_soft_knee"), &YutrelEnvironment::get_bloom_soft_knee);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bloom_soft_knee", PROPERTY_HINT_RANGE, "0.0,1.0,0.02"), "set_bloom_soft_knee", "get_bloom_soft_knee");
	ClassDB::bind_method(D_METHOD("set_bloom_scatter", "value"), &YutrelEnvironment::set_bloom_scatter);
	ClassDB::bind_method(D_METHOD("get_bloom_scatter"), &YutrelEnvironment::get_bloom_scatter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bloom_scatter", PROPERTY_HINT_RANGE, "0.05,0.95,0.02"), "set_bloom_scatter", "get_bloom_scatter");

	ADD_GROUP("Color Grading", "grading_");
	ClassDB::bind_method(D_METHOD("set_grading_temperature", "value"), &YutrelEnvironment::set_grading_temperature);
	ClassDB::bind_method(D_METHOD("get_grading_temperature"), &YutrelEnvironment::get_grading_temperature);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grading_temperature", PROPERTY_HINT_RANGE, "-1.0,1.0,0.01"), "set_grading_temperature", "get_grading_temperature");
	ClassDB::bind_method(D_METHOD("set_grading_tint", "value"), &YutrelEnvironment::set_grading_tint);
	ClassDB::bind_method(D_METHOD("get_grading_tint"), &YutrelEnvironment::get_grading_tint);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grading_tint", PROPERTY_HINT_RANGE, "-1.0,1.0,0.01"), "set_grading_tint", "get_grading_tint");
	ClassDB::bind_method(D_METHOD("set_grading_contrast", "value"), &YutrelEnvironment::set_grading_contrast);
	ClassDB::bind_method(D_METHOD("get_grading_contrast"), &YutrelEnvironment::get_grading_contrast);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grading_contrast", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_grading_contrast", "get_grading_contrast");
	ClassDB::bind_method(D_METHOD("set_grading_saturation", "value"), &YutrelEnvironment::set_grading_saturation);
	ClassDB::bind_method(D_METHOD("get_grading_saturation"), &YutrelEnvironment::get_grading_saturation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grading_saturation", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_grading_saturation", "get_grading_saturation");
	ClassDB::bind_method(D_METHOD("set_grading_shadows", "value"), &YutrelEnvironment::set_grading_shadows);
	ClassDB::bind_method(D_METHOD("get_grading_shadows"), &YutrelEnvironment::get_grading_shadows);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "grading_shadows", PROPERTY_HINT_COLOR_NO_ALPHA), "set_grading_shadows", "get_grading_shadows");
	ClassDB::bind_method(D_METHOD("set_grading_midtones", "value"), &YutrelEnvironment::set_grading_midtones);
	ClassDB::bind_method(D_METHOD("get_grading_midtones"), &YutrelEnvironment::get_grading_midtones);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "grading_midtones", PROPERTY_HINT_COLOR_NO_ALPHA), "set_grading_midtones", "get_grading_midtones");
	ClassDB::bind_method(D_METHOD("set_grading_highlights", "value"), &YutrelEnvironment::set_grading_highlights);
	ClassDB::bind_method(D_METHOD("get_grading_highlights"), &YutrelEnvironment::get_grading_highlights);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "grading_highlights", PROPERTY_HINT_COLOR_NO_ALPHA), "set_grading_highlights", "get_grading_highlights");
	ClassDB::bind_method(D_METHOD("set_grading_lut", "value"), &YutrelEnvironment::set_grading_lut);
	ClassDB::bind_method(D_METHOD("get_grading_lut"), &YutrelEnvironment::get_grading_lut);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "grading_lut", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_grading_lut", "get_grading_lut");
	ClassDB::bind_method(D_METHOD("set_grading_lut_intensity", "value"), &YutrelEnvironment::set_grading_lut_intensity);
	ClassDB::bind_method(D_METHOD("get_grading_lut_intensity"), &YutrelEnvironment::get_grading_lut_intensity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grading_lut_intensity", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_grading_lut_intensity", "get_grading_lut_intensity");
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

void YutrelEnvironment::set_upscaler_mode(UpscalerMode p_value) {
	ERR_FAIL_COND(p_value < UPSCALER_MODE_OFF || p_value > UPSCALER_MODE_FSR2);
	upscaler_mode = p_value;
	_publish_settings();
}

void YutrelEnvironment::set_upscaler_quality(UpscalerQuality p_value) {
	ERR_FAIL_COND(p_value < UPSCALER_QUALITY_NATIVE_AA || p_value > UPSCALER_QUALITY_PERFORMANCE);
	upscaler_quality = p_value;
	_publish_settings();
}

void YutrelEnvironment::set_upscaler_sharpness(float p_value) {
	ERR_FAIL_COND(!Math::is_finite(p_value));
	upscaler_sharpness = CLAMP(p_value, 0.0f, 1.0f);
	_publish_settings();
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

void YutrelEnvironment::set_vignette_intensity(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	vignette_intensity = CLAMP(p_value, 0.0f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::set_vignette_smoothness(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	vignette_smoothness = CLAMP(p_value, 0.05f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::set_debanding_enabled(bool p_value) {
	debanding_enabled = p_value;
	_publish_settings();
}

void YutrelEnvironment::set_auto_exposure_enabled(bool p_value) {
	auto_exposure_enabled = p_value;
	_publish_settings();
}

void YutrelEnvironment::set_auto_exposure_min_ev(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	auto_exposure_min_ev = CLAMP(p_value, -12.0f, 12.0f);
	_publish_settings();
}

void YutrelEnvironment::set_auto_exposure_max_ev(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	auto_exposure_max_ev = CLAMP(p_value, -12.0f, 12.0f);
	_publish_settings();
}

void YutrelEnvironment::set_auto_exposure_compensation(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	auto_exposure_compensation = CLAMP(p_value, -8.0f, 8.0f);
	_publish_settings();
}

void YutrelEnvironment::set_auto_exposure_speed_up(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	auto_exposure_speed_up = CLAMP(p_value, 0.01f, 20.0f);
	_publish_settings();
}

void YutrelEnvironment::set_auto_exposure_speed_down(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	auto_exposure_speed_down = CLAMP(p_value, 0.01f, 20.0f);
	_publish_settings();
}

void YutrelEnvironment::set_bloom_enabled(bool p_value) {
	bloom_enabled = p_value;
	_publish_settings();
}

void YutrelEnvironment::set_bloom_intensity(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	bloom_intensity = CLAMP(p_value, 0.0f, 4.0f);
	_publish_settings();
}

void YutrelEnvironment::set_bloom_threshold(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	bloom_threshold = CLAMP(p_value, 0.0f, 64.0f);
	_publish_settings();
}

void YutrelEnvironment::set_bloom_soft_knee(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	bloom_soft_knee = CLAMP(p_value, 0.0f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::set_bloom_scatter(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	bloom_scatter = CLAMP(p_value, 0.05f, 0.95f);
	_publish_settings();
}

void YutrelEnvironment::set_grading_temperature(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	grading_temperature = CLAMP(p_value, -1.0f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::set_grading_tint(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	grading_tint = CLAMP(p_value, -1.0f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::set_grading_contrast(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	grading_contrast = CLAMP(p_value, 0.0f, 2.0f);
	_publish_settings();
}

void YutrelEnvironment::set_grading_saturation(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	grading_saturation = CLAMP(p_value, 0.0f, 2.0f);
	_publish_settings();
}

void YutrelEnvironment::set_grading_shadows(const Color &p_value) {
	if (!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b) || !Math::is_finite(p_value.a)) {
		return;
	}
	grading_shadows = Color(CLAMP(p_value.r, 0.0f, 1.0f), CLAMP(p_value.g, 0.0f, 1.0f), CLAMP(p_value.b, 0.0f, 1.0f));
	_publish_settings();
}

void YutrelEnvironment::set_grading_midtones(const Color &p_value) {
	if (!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b) || !Math::is_finite(p_value.a)) {
		return;
	}
	grading_midtones = Color(CLAMP(p_value.r, 0.0f, 1.0f), CLAMP(p_value.g, 0.0f, 1.0f), CLAMP(p_value.b, 0.0f, 1.0f));
	_publish_settings();
}

void YutrelEnvironment::set_grading_highlights(const Color &p_value) {
	if (!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b) || !Math::is_finite(p_value.a)) {
		return;
	}
	grading_highlights = Color(CLAMP(p_value.r, 0.0f, 1.0f), CLAMP(p_value.g, 0.0f, 1.0f), CLAMP(p_value.b, 0.0f, 1.0f));
	_publish_settings();
}

void YutrelEnvironment::set_grading_lut(const Ref<Texture2D> &p_value) {
	if (grading_lut == p_value) {
		return;
	}
	if (grading_lut.is_valid()) {
		grading_lut->disconnect_changed(callable_mp(this, &YutrelEnvironment::_grading_lut_changed));
	}
	grading_lut = p_value;
	if (grading_lut.is_valid()) {
		grading_lut->connect_changed(callable_mp(this, &YutrelEnvironment::_grading_lut_changed));
	}
	_grading_lut_changed();
}

void YutrelEnvironment::set_grading_lut_intensity(float p_value) {
	if (!Math::is_finite(p_value)) {
		return;
	}
	grading_lut_intensity = CLAMP(p_value, 0.0f, 1.0f);
	_publish_settings();
}

void YutrelEnvironment::_grading_lut_changed() {
	++grading_lut_revision;
	_publish_settings();
}

void YutrelEnvironment::_publish_settings() {
	if (world.is_valid()) {
		YutrelEnvironmentSettings settings;
		settings.tone_mapping = ToneMappingMode(tone_mapping);
		settings.exposure = exposure;
		settings.sky_mode = RendererSceneRenderImplementation::SkyMode(sky_mode);
		settings.linear_sky_color = sky_color.srgb_to_linear();
		settings.sky_texture_rid = sky_texture.is_valid() ? sky_texture->get_rid() : RID();
		settings.sky_intensity = sky_intensity;
		settings.sky_rotation_radians = Math::deg_to_rad(sky_rotation);
		settings.shadow_mode = RendererSceneRenderImplementation::ShadowMode(shadow_mode);
		settings.shadow_resolution = shadow_resolution;
		settings.shadow_rt_half_resolution = shadow_rt_half_resolution;
		settings.shadow_rt_samples = shadow_rt_samples;
		settings.rtao_enabled = rtao_enabled;
		settings.rtao_half_resolution = rtao_half_resolution;
		settings.rtao_radius = rtao_radius;
		settings.rtao_intensity = rtao_intensity;
		settings.rtao_samples = rtao_samples;
		settings.rt_reflection_enabled = rt_reflection_enabled;
		settings.rt_reflection_half_resolution = rt_reflection_half_resolution;
		settings.rt_reflection_max_roughness = rt_reflection_max_roughness;
		settings.rt_reflection_samples = rt_reflection_samples;
		settings.upscaler_mode = RendererSceneRenderImplementation::UpscalerMode(upscaler_mode);
		settings.upscaler_quality = RendererSceneRenderImplementation::UpscalerQuality(upscaler_quality);
		settings.upscaler_sharpness = upscaler_sharpness;
		settings.vignette_intensity = vignette_intensity;
		settings.vignette_smoothness = vignette_smoothness;
		settings.debanding_enabled = debanding_enabled;
		settings.auto_exposure_enabled = auto_exposure_enabled;
		settings.auto_exposure_min_ev = auto_exposure_min_ev;
		settings.auto_exposure_max_ev = auto_exposure_max_ev;
		settings.auto_exposure_compensation = auto_exposure_compensation;
		settings.auto_exposure_speed_up = auto_exposure_speed_up;
		settings.auto_exposure_speed_down = auto_exposure_speed_down;
		settings.bloom_enabled = bloom_enabled;
		settings.bloom_intensity = bloom_intensity;
		settings.bloom_threshold = bloom_threshold;
		settings.bloom_soft_knee = bloom_soft_knee;
		settings.bloom_scatter = bloom_scatter;
		settings.grading_temperature = grading_temperature;
		settings.grading_tint = grading_tint;
		settings.grading_contrast = grading_contrast;
		settings.grading_saturation = grading_saturation;
		settings.grading_shadows = grading_shadows.srgb_to_linear();
		settings.grading_midtones = grading_midtones.srgb_to_linear();
		settings.grading_highlights = grading_highlights.srgb_to_linear();
		settings.grading_lut_rid = grading_lut.is_valid() ? grading_lut->get_rid() : RID();
		settings.grading_lut_revision = grading_lut_revision;
		settings.grading_lut_intensity = grading_lut_intensity;
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
