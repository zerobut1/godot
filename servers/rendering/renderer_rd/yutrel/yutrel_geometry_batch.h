/**************************************************************************/
/*  yutrel_geometry_batch.h                                               */
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
#include "yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

// Base and shadow passes share mesh decoding, material overrides and winding.
class YutrelGeometryBatch {
protected:
	struct DrawItem {
		void *surface = nullptr;
		bool mirrored = false;
		bool double_sided = false;
		RID vertex_array;
		RID index_array;
		RID pipeline;
	};

	YutrelShader shader;
	PipelineCacheRD pipelines[2][2];
	YutrelStorageBuffer instance_buffer;
	YutrelStorageBuffer material_buffer;
	LocalVector<DrawItem> draws;
	LocalVector<YutrelInstanceData> instances;
	LocalVector<YutrelMaterialData> materials;
	LocalVector<RID> material_sets;
	HashMap<RID, int> material_indices;

	bool prepare_geometry(bool p_shadow);
	bool upload_geometry();
	void resolve_draws(RD::FramebufferFormatID p_format, bool p_shadow);
	void add_geometry(const RenderGeometryInstanceBase &p_instance, bool p_camera_mirrored, YutrelMaterialBridge &p_bridge, bool p_shadow);
	void draw_range(RD::DrawListID p_list, uint32_t p_begin, uint32_t p_end, const float (*p_shadow_vp)[4] = nullptr);

public:
	void clear();
	bool has_draws() const { return !draws.is_empty(); }
};

} // namespace RendererSceneRenderImplementation
