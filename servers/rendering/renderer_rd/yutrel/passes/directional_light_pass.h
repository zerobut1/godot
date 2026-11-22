/**************************************************************************/
/*  directional_light_pass.h                                              */
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

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/csm_shadow_data.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_shader.h"

class RenderDataRD;

namespace RendererSceneRenderImplementation {

struct DirectionalLightParams {
	float direction[4] = {};
	float radiance[3] = {};
	uint32_t has_shadow = 0;
};
static_assert(sizeof(DirectionalLightParams) == 32);

struct DirectionalLightItem {
	RID instance;
	DirectionalLightParams params;
	CSMShadowData shadow;
};

struct YutrelPipelineTargets;

class DirectionalLightPass {
	YutrelShader lighting_shader;
	YutrelShader dfg_shader;
	PipelineCacheRD lighting_pipeline;
	RID dfg_lut;
	uint64_t dfg_baked_version = 0;

	bool _bake_dfg_lut();

public:
	~DirectionalLightPass();

	bool prepare();
	void initialize_dfg_lut();
	bool prepare_dfg_lut();
	RID get_dfg_lut() const { return dfg_lut; }

	static LocalVector<DirectionalLightItem> collect(const RenderDataRD &p_data);
	void render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const DirectionalLightParams &p_light, RID p_mask, bool p_clear);
};

} // namespace RendererSceneRenderImplementation
