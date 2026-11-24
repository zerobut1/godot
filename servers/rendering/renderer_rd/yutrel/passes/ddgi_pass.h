/**************************************************************************/
/*  ddgi_pass.h                                                           */
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
#include "local_light_pass.h"
#include "ibl_pass.h"

#include "core/math/random_pcg.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_ddgi_settings.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_rt_scene.h"

namespace RendererSceneRenderImplementation {

class DDGIPass {
	YutrelComputeShader trace;
	YutrelComputeShader blend_irradiance;
	YutrelComputeShader blend_distance;
	YutrelComputeShader relocate;
	YutrelComputeShader classify;
	YutrelShader debug_shader;
	PipelineCacheRD debug_pipeline;
	YutrelStorageBuffer light_buffer;

public:
	// Owned by the render buffers. Viewports can have different scenarios and RT layer masks.
	struct History {
		DDGIBindings bindings;
		RID ray_data;
		Vector3i counts;
		int rays = 0;
		bool used_fixed_rays = false;
		uint64_t config_version = 0;
		uint64_t shader_version = 0;
		RandomPCG rotations;
#ifdef TOOLS_ENABLED
		uint32_t converged_frames = 0;
		uint64_t last_state_hash = 0;
#endif

		void clear();
		~History() { clear(); }
	};

	bool update(History &r_history, const YutrelDDGISettings &p_volume, const LocalVector<DirectionalLightItem> &p_lights, const LocalLightPass &p_local,
			const IBLPass &p_ibl, const YutrelSkyParams &p_sky, YutrelRayTracingScene &p_scene, double p_time_step);
	void draw_probes(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const History &p_history, const YutrelEnvironmentSettings &p_environment);
};

} // namespace RendererSceneRenderImplementation
