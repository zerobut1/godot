/**************************************************************************/
/*  editor_gizmo_pass.h                                                  */
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

#ifdef TOOLS_ENABLED

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_shader.h"

class RenderGeometryInstanceBase;
class RenderSceneDataRD;

namespace RendererSceneRenderImplementation {

class EditorGizmoPass {
	enum Primitive {
		TRIANGLES,
		LINES,
		LINE_STRIP,
		POINTS,
		PRIMITIVE_COUNT,
	};

	struct FrameData {
		float view_projection[4][4];
		float camera_x[4];
		float camera_y[4];
		float camera_z[4];
		float camera_position[4];
		float inverse_viewport_size[2];
		float orthogonal_scale;
		uint32_t convert_to_srgb;
	};
	static_assert(sizeof(FrameData) == 144);
	static_assert(offsetof(FrameData, inverse_viewport_size) == 128);
	static_assert(offsetof(FrameData, convert_to_srgb) == 140);

	struct GizmoData {
		YutrelInstanceData instance;
		float albedo[4];
		float modulation[4];
		float uv_scale[2];
		float alpha_scissor;
		float grid_size;
		float point_size;
		uint32_t flags;
		float padding[2] = {};
	};
	static_assert(sizeof(GizmoData) == 256);
	static_assert(offsetof(GizmoData, albedo) == 192);
	static_assert(offsetof(GizmoData, uv_scale) == 224);
	static_assert(offsetof(GizmoData, flags) == 244);

	struct DrawItem {
		void *surface = nullptr;
		GizmoData data = {};
		RID texture;
		Primitive primitive = TRIANGLES;
		bool on_top = false;
		int32_t priority = 0;
		real_t depth = 0;
		uint32_t point_instances = 0;
		RID uniform_set;
		RID pipeline;
		RID vertex_array;
		RID index_array;

		bool operator<(const DrawItem &p_other) const {
			if (on_top != p_other.on_top) {
				return !on_top;
			}
			if (priority != p_other.priority) {
				return priority < p_other.priority;
			}
			return depth > p_other.depth;
		}
	};

	YutrelShader shader;
	PipelineCacheRD pipelines[PRIMITIVE_COUNT][2];
	RID frame_buffer;
	RID white_texture;
	YutrelStorageBuffer instance_buffer;
	LocalVector<DrawItem> draws;
	LocalVector<GizmoData> instances;

	bool _prepare_shader();
	void _add_mesh(const RenderGeometryInstanceBase &p_instance, RID p_mesh, const Transform3D &p_transform, const Transform3D &p_camera, const Color &p_modulation);

public:
	~EditorGizmoPass();

	bool has_draws() const { return !draws.is_empty(); }
	void clear() { draws.clear(); }
	void add(const RenderGeometryInstanceBase &p_instance, const Transform3D &p_camera);
	void render(const YutrelPipelineTargets &p_targets, const RenderSceneDataRD &p_scene, const YutrelFrameData &p_frame);
};

} // namespace RendererSceneRenderImplementation

#endif // TOOLS_ENABLED
