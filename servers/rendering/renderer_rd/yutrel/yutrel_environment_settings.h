/**************************************************************************/
/*  yutrel_environment_settings.h                                         */
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

#include "core/math/color.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/rid.h"

namespace RendererSceneRenderImplementation {

// Mode values are shared with YutrelEnvironment and output.slang.
enum class ToneMappingMode : uint32_t {
	AGX,
	AGX_PUNCHY,
	ACES,
	NONE,
};

enum class ShadowMode : uint32_t {
	CSM,
	RAY_TRACED,
};

enum class SkyMode : uint32_t {
	CONSTANT_COLOR,
	HDRI,
};

enum class UpscalerMode : uint32_t {
	OFF,
	FSR2,
};

enum class UpscalerQuality : uint32_t {
	NATIVE_AA,
	QUALITY,
	BALANCED,
	PERFORMANCE,
};

// Shared push constant prefix for ibl.slang and skybox.slang.
struct YutrelSkyParams {
	float intensity;
	float sin_rot;
	float cos_rot;
};

static_assert(sizeof(YutrelSkyParams) == 12);

struct YutrelEnvironmentSettings {
	ToneMappingMode tone_mapping = ToneMappingMode::AGX_PUNCHY;
	float exposure = 1.0f;
	SkyMode sky_mode = SkyMode::CONSTANT_COLOR;
	Color linear_sky_color = Color(0.08f, 0.08f, 0.08f);
	RID sky_texture_rid;
	float sky_intensity = 1.0f;
	float sky_rotation_radians = 0.0f;
	ShadowMode shadow_mode = ShadowMode::RAY_TRACED;
	int shadow_resolution = 2048;
	bool shadow_rt_half_resolution = true;
	int shadow_rt_samples = 1;
	bool rtao_enabled = true;
	bool rtao_half_resolution = true;
	float rtao_radius = 1.0f;
	float rtao_intensity = 1.0f;
	int rtao_samples = 1;
	bool rt_reflection_enabled = true;
	bool rt_reflection_half_resolution = true;
	float rt_reflection_max_roughness = 0.6f;
	int rt_reflection_samples = 1;
	UpscalerMode upscaler_mode = UpscalerMode::OFF;
	UpscalerQuality upscaler_quality = UpscalerQuality::NATIVE_AA;
	float upscaler_sharpness = 0.2f;
	float vignette_intensity = 0.0f;
	float vignette_smoothness = 0.5f;
	bool debanding_enabled = true;
	bool auto_exposure_enabled = false;
	float auto_exposure_min_ev = -6.0f;
	float auto_exposure_max_ev = 6.0f;
	float auto_exposure_compensation = 0.0f;
	float auto_exposure_speed_up = 3.0f;
	float auto_exposure_speed_down = 1.5f;
	bool bloom_enabled = false;
	float bloom_intensity = 0.15f;
	float bloom_threshold = 1.0f;
	float bloom_soft_knee = 0.5f;
	float bloom_scatter = 0.65f;
	float grading_temperature = 0.0f;
	float grading_tint = 0.0f;
	float grading_contrast = 1.0f;
	float grading_saturation = 1.0f;
	Color grading_shadows = Color(1, 1, 1);
	Color grading_midtones = Color(1, 1, 1);
	Color grading_highlights = Color(1, 1, 1);
	RID grading_lut_rid;
	uint64_t grading_lut_revision = 0;
	float grading_lut_intensity = 1.0f;
	uint64_t revision = 0;

	static YutrelEnvironmentSettings get(RID p_environment) {
		if (p_environment.is_null()) {
			return {};
		}
		MutexLock lock(mutex);
		const YutrelEnvironmentSettings *settings = settings_by_environment.getptr(p_environment);
		return settings ? *settings : YutrelEnvironmentSettings();
	}

	static bool has(RID p_environment) {
		MutexLock lock(mutex);
		return settings_by_environment.has(p_environment);
	}

	static void set(RID p_environment, const YutrelEnvironmentSettings &p_settings) {
		MutexLock lock(mutex);
		settings_by_environment.insert(p_environment, p_settings)->value.revision = ++revision_counter;
	}

	static void erase(RID p_environment) {
		MutexLock lock(mutex);
		settings_by_environment.erase(p_environment);
	}

private:
	static inline uint64_t revision_counter = 0;
	static inline Mutex mutex;
	static inline HashMap<RID, YutrelEnvironmentSettings> settings_by_environment;
};

} // namespace RendererSceneRenderImplementation
