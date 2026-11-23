/**************************************************************************/
/*  rt_reflection_pass.h                                                  */
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

#include "../yutrel_rt_scene.h"

namespace RendererSceneRenderImplementation {

class RTReflectionPass {
	YutrelComputeShader trace;
	YutrelComputeShader temporal;
	YutrelComputeShader denoise;
	YutrelComputeShader upsample;
	YutrelStorageBuffer light_buffer;
	RID fallback_reflection;

public:
	// G-buffer A/B/C, depth, source and prefiltered sky, and three DDGI atlases.
	static constexpr uint32_t RT_SAMPLED_IMAGES = 9;

	struct History {
		RID reflection_texture, raw_reflection, filtered_reflection;
		RID history[2], prev_depth[2], prev_normal_roughness[2], hit_dist[2];
		RID prev_frame_buffer;
		YutrelFrameData prev_frame;
		bool has_history = false;
		uint32_t frame_index = 0;
		uint32_t history_slot = 0;
		Size2i full_size, work_size;
		uint64_t shader_version = 0;
		uint64_t lighting_hash = 0;
		float last_max_roughness = 0;
		int last_samples = 0;
		bool last_half_resolution = true;
#ifdef TOOLS_ENABLED
		uint64_t last_scene_hash = 0;
		uint32_t converged_frames = 0;
#endif

		bool ensure_size(Size2i p_full_size, Size2i p_work_size);
		void clear();
		~History() { clear(); }
	};

	~RTReflectionPass();
	RID get_fallback_reflection();
	bool render(History &r_history, const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame,
			const YutrelEnvironmentSettings &p_environment, const LocalVector<DirectionalLightItem> &p_lights, const IBLPass &p_ibl,
			const YutrelSkyParams &p_sky, const DDGIBindings &p_ddgi, YutrelRayTracingScene &p_scene, double p_time_step);
};

} // namespace RendererSceneRenderImplementation
