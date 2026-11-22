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

enum class SkyMode : uint32_t {
	CONSTANT_COLOR,
	HDRI,
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
		settings_by_environment.insert(p_environment, p_settings);
	}

	static void erase(RID p_environment) {
		MutexLock lock(mutex);
		settings_by_environment.erase(p_environment);
	}

private:
	static inline Mutex mutex;
	static inline HashMap<RID, YutrelEnvironmentSettings> settings_by_environment;
};

} // namespace RendererSceneRenderImplementation
