/**************************************************************************/
/*  yutrel_material_settings.h                                            */
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

#include "yutrel_material.h"

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"

namespace RendererSceneRenderImplementation {

struct YutrelMaterialSettings {
	YutrelMaterialData data;
	RID textures[YutrelMaterialBridge::TEXTURE_COUNT]; // RenderingServer texture RIDs.
	int32_t render_priority = 0;

	static bool get(RID p_rid, YutrelMaterialSettings &r_settings) {
		MutexLock lock(mutex);
		const YutrelMaterialSettings *settings = materials.getptr(p_rid);
		if (!settings) {
			return false;
		}
		r_settings = *settings;
		return true;
	}

	static void set(RID p_rid, const YutrelMaterialSettings &p_settings) {
		MutexLock lock(mutex);
		materials.insert(p_rid, p_settings);
	}

	static void erase(RID p_rid) {
		MutexLock lock(mutex);
		materials.erase(p_rid);
	}

private:
	static inline Mutex mutex;
	static inline HashMap<RID, YutrelMaterialSettings> materials;
};

} // namespace RendererSceneRenderImplementation
