/**************************************************************************/
/*  yutrel_ddgi_settings.cpp                                              */
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

#include "yutrel_ddgi_settings.h"

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

namespace RendererSceneRenderImplementation {

namespace {
Mutex registry_mutex;
// Insertion order gives each scenario a stable active volume and a successor on removal.
HashMap<RID, Vector<YutrelDDGISettings>> volumes;
uint64_t next_version = 1;
} // namespace

bool YutrelDDGISettings::get(RID p_scenario, YutrelDDGISettings &r_active) {
	MutexLock lock(registry_mutex);
	const auto *entries = volumes.getptr(p_scenario);
	if (!entries) {
		return false;
	}
	r_active = (*entries)[0];
	return true;
}

bool YutrelDDGISettings::publish(RID p_scenario, YutrelDDGISettings p_settings) {
	MutexLock lock(registry_mutex);
	auto &entries = volumes[p_scenario];
	for (int i = 0; i < entries.size(); i++) {
		const auto &old = entries[i];
		if (old.owner == p_settings.owner) {
			const bool reset = old.origin != p_settings.origin || old.size != p_settings.size ||
					old.probe_counts != p_settings.probe_counts || old.irradiance_gamma != p_settings.irradiance_gamma ||
					old.probe_relocation != p_settings.probe_relocation || old.probe_classification != p_settings.probe_classification;
			p_settings.config_version = reset ? next_version++ : old.config_version;
			entries.write[i] = p_settings;
			return false;
		}
	}
	p_settings.config_version = next_version++;
	entries.push_back(p_settings);
	return true;
}

bool YutrelDDGISettings::erase(RID p_scenario, ObjectID p_owner) {
	MutexLock lock(registry_mutex);
	auto *entries = volumes.getptr(p_scenario);
	if (entries) {
		for (int i = 0; i < entries->size(); i++) {
			if ((*entries)[i].owner == p_owner) {
				entries->remove_at(i);
				if (entries->is_empty()) {
					volumes.erase(p_scenario);
				}
				return true;
			}
		}
	}
	return false;
}

bool YutrelDDGISettings::has_multiple(RID p_scenario) {
	MutexLock lock(registry_mutex);
	const auto *entries = volumes.getptr(p_scenario);
	return entries && entries->size() > 1;
}

} // namespace RendererSceneRenderImplementation
