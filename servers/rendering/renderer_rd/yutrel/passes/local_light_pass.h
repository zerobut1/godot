/**************************************************************************/
/*  local_light_pass.h                                                    */
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

#include "../yutrel_render_data.h"

class RenderDataRD;

namespace RendererSceneRenderImplementation {

class YutrelRayTracingScene;

class LocalLightPass {
	YutrelStorageBuffer local_light_buffer;
	YutrelComputeShader cluster_build;
	YutrelShader shader, rt_shader;
	PipelineCacheRD pipeline, rt_pipeline;
	RID grid, indices, params_buffer;
	uint32_t light_count = 0;
	uint64_t light_hash = 0;
	bool shadowed = false;

public:
	static constexpr uint32_t RT_SAMPLED_IMAGES = 5; // Three G-buffers, depth and DFG.

	~LocalLightPass();
	bool collect(const RenderDataRD &p_data);
	bool build_clusters(const RenderDataRD &p_data, const Rect2i &p_region, const Projection &p_projection);
	void render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_dfg_lut, YutrelRayTracingScene *p_scene, bool p_clear);
	RID get_clustered_set(RID p_shader, LocalVector<RD::Uniform> p_uniforms) const;
	RD::Uniform light_uniform() const { return RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 16, local_light_buffer.get_buffer()); }
	uint32_t get_count() const { return light_count; }
	uint64_t get_hash() const { return light_hash; }
	bool has_shadows() const { return shadowed; }
};

} // namespace RendererSceneRenderImplementation
