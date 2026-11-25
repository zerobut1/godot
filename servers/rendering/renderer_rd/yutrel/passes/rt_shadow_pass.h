/**************************************************************************/
/*  rt_shadow_pass.h                                                      */
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

#include "servers/rendering/renderer_rd/yutrel/yutrel_environment_settings.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_rt_scene.h"

namespace RendererSceneRenderImplementation {

struct DirectionalLightItem;

class RTShadowPass {
	YutrelComputeShader trace, temporal, denoise, upsample;

	struct LightHistory {
		RID visibility[2];
		float direction[4] = {};
		int samples = 0;
		uint32_t frame_index = 0;
		uint64_t shader_version = 0;
		bool rendered = false;
#ifdef TOOLS_ENABLED
		uint32_t converged_frames = 0;
		uint64_t last_scene_hash = 0;
#endif
		void clear();
	};

public:
	struct History {
		HashMap<RID, LightHistory> lights;
		RID raw_shadow, denoise_temp, filtered_shadow;
		RID prev_depth[2], prev_normal[2], prev_frame_buffer;
		YutrelFrameData prev_frame;
		Size2i full_size;
		bool half_resolution = true;
		uint32_t slot = 0;
		bool has_history = false;
		bool wrote_surfaces = false;

		bool ensure_size(Size2i p_full_size, bool p_half_resolution, bool p_soft);
		void finish_frame(const YutrelFrameData &p_frame);
		void invalidate();
		void clear();
		~History() { clear(); }
	};

	bool render(History &r_history, const DirectionalLightItem &p_light, const YutrelPipelineTargets &p_targets,
			RID p_frame_buffer, const YutrelFrameData &p_frame, const YutrelEnvironmentSettings &p_environment,
			YutrelRayTracingScene &p_scene, double p_time_step);
};

} // namespace RendererSceneRenderImplementation
