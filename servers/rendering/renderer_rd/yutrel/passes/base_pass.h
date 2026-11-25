/**************************************************************************/
/*  base_pass.h                                                           */
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

#include "servers/rendering/renderer_rd/yutrel/yutrel_geometry_batch.h"

namespace RendererSceneRenderImplementation {

class BasePass : public YutrelGeometryBatch {
	struct FrontToBack {
		bool operator()(const DrawItem &a, const DrawItem &b) const {
			if (a.material.alpha_scissor != b.material.alpha_scissor) {
				return !a.material.alpha_scissor;
			}
			if (a.distance != b.distance) {
				return a.distance < b.distance;
			}
			return a.instance_id < b.instance_id;
		}
	};

	struct ByPipelineAndMaterial {
		bool operator()(const DrawItem &a, const DrawItem &b) const {
			if (a.pipeline != b.pipeline) {
				return a.pipeline.get_id() < b.pipeline.get_id();
			}
			if (a.material.id != b.material.id) {
				return a.material.id < b.material.id;
			}
			return a.instance_id < b.instance_id;
		}
	};

	YutrelShader depth_shader;
	CullPipelines depth_pipelines;

	RID _get_frame_set(RID p_shader, RID p_frame_buffer) const;
	bool _render_depth(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_material_sampler);

public:
	BasePass() : YutrelGeometryBatch(BASE) {}
	bool prepare();
	void add(const RenderGeometryInstanceBase &p_instance, bool p_camera_mirrored, YutrelMaterialBridge &p_material_bridge,
			YutrelGeometryBatch &p_transparent, const Vector3 &p_camera_position, const Vector3 &p_camera_forward, const Transform3D &p_prev_transform) {
		add_geometry(p_instance, p_camera_mirrored, p_material_bridge, &p_transparent, p_camera_position, p_camera_forward, &p_prev_transform);
	}
	bool render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_material_sampler, const Color &p_clear_color, RenderingServerTypes::RenderInfo *p_render_info = nullptr);
};

} // namespace RendererSceneRenderImplementation
