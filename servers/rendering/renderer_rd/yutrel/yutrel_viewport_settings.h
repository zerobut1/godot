/**************************************************************************/
/*  yutrel_viewport_settings.h                                            */
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

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/rid.h"

namespace RendererSceneRenderImplementation {

// Mode values are shared with debug_view.slang and rt_debug_view.slang.
enum class DebugViewMode : uint32_t {
	FINAL,
	ALBEDO,
	WORLD_NORMAL,
	ROUGHNESS,
	METALLIC,
	MATERIAL_AO,
	LIGHTING,
	DEPTH,
	SHADOW_MASK,
	CSM_CASCADES,
	PATH_TRACER,
	DDGI_INDIRECT,
	DDGI_PROBES,
	RTAO,
	RT_REFLECTION,
	RT_ALBEDO = 15,
	RT_WORLD_NORMAL = 16,
	RT_GEOMETRIC_NORMAL = 17,
	RT_ROUGHNESS = 18,
	RT_METALLIC = 19,
	RT_EMISSIVE = 20,
	RT_HIT_FACE = 21,
	MOTION_VECTORS = 22,
};

inline bool is_rt_surface_debug_view(DebugViewMode p_mode) {
	return uint32_t(p_mode) >= uint32_t(DebugViewMode::RT_ALBEDO) && uint32_t(p_mode) <= uint32_t(DebugViewMode::RT_HIT_FACE);
}

struct YutrelViewportSettings {
	DebugViewMode debug_view = DebugViewMode::FINAL;

	static YutrelViewportSettings get(RID p_render_target, DebugViewMode p_default = DebugViewMode::FINAL) {
		MutexLock lock(mutex);
		const YutrelViewportSettings *settings = settings_by_target.getptr(p_render_target);
		return settings ? *settings : YutrelViewportSettings{ p_default };
	}

	static void set(RID p_render_target, const YutrelViewportSettings &p_settings) {
		if (p_render_target.is_valid()) {
			MutexLock lock(mutex);
			settings_by_target.insert(p_render_target, p_settings);
		}
	}

	static void erase(RID p_render_target) {
		MutexLock lock(mutex);
		settings_by_target.erase(p_render_target);
	}

private:
	static inline Mutex mutex;
	static inline HashMap<RID, YutrelViewportSettings> settings_by_target;
};

} // namespace RendererSceneRenderImplementation
