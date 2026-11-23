/**************************************************************************/
/*  transparent_pass.h                                                    */
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

#include "directional_light_pass.h"
#include "ibl_pass.h"

#include "servers/rendering/renderer_rd/yutrel/yutrel_geometry_batch.h"

namespace RendererSceneRenderImplementation {

class TransparentPass : public YutrelGeometryBatch {
	struct BackToFront {
		bool operator()(const DrawItem &a, const DrawItem &b) const {
			if (a.material.priority != b.material.priority) {
				return a.material.priority < b.material.priority;
			}
			if (a.distance != b.distance) {
				return a.distance > b.distance;
			}
			return a.instance_id < b.instance_id;
		}
	};

	YutrelStorageBuffer light_buffer;
	YutrelStorageBuffer shadow_buffer;
	RID fallback_depth;
	RID fallback_sh;

	bool _upload_lights(const LocalVector<DirectionalLightItem> &p_lights);

public:
	TransparentPass() : YutrelGeometryBatch(TRANSPARENT) {}
	~TransparentPass();
	void render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const LocalVector<DirectionalLightItem> &p_lights,
			RID p_shadow_array, RID p_comparison_sampler, RID p_dfg_lut, const IBLPass &p_ibl, bool p_has_ibl, const YutrelSkyParams &p_sky, const DDGIBindings &p_ddgi,
			RenderingServerTypes::RenderInfo *p_render_info = nullptr);
};

} // namespace RendererSceneRenderImplementation
