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

class YutrelGeometryBatch {
protected:
	enum Pass {
		BASE,
		SHADOW,
		TRANSPARENT,
	};
	const Pass pass;

	struct BatchMaterial {
		int id = -1;
		bool transparent = false;
		int32_t priority = 0;
		YutrelMaterialBridge::BlendMode blend = YutrelMaterialBridge::BLEND_MIX;
	};

	struct DrawItem {
		uint32_t instance_id = 0;
		uint32_t primitive_count = 0;
		float distance = 0;
		BatchMaterial material;
		void *surface = nullptr;
		bool mirrored = false;
		bool double_sided = false;
		RID vertex_array;
		RID index_array;
		RID pipeline;
	};

	YutrelShader shader;
	PipelineCacheRD pipelines[YutrelMaterialBridge::BLEND_MODE_COUNT][2][2];
	YutrelStorageBuffer instance_buffer;
	YutrelStorageBuffer material_buffer;
	LocalVector<DrawItem> draws;
	LocalVector<YutrelInstanceData> instances;
	LocalVector<YutrelMaterialData> materials;
	LocalVector<RID> material_sets;
	HashMap<RID, BatchMaterial> material_indices;

	explicit YutrelGeometryBatch(Pass p_pass) : pass(p_pass) {}
	bool upload_geometry();
	void resolve_draws(RD::FramebufferFormatID p_format);
	BatchMaterial resolve_material(RID p_material, YutrelMaterialBridge &p_bridge, YutrelGeometryBatch *p_transparent);
	void add_geometry(const RenderGeometryInstanceBase &p_instance, bool p_camera_mirrored, YutrelMaterialBridge &p_bridge,
			YutrelGeometryBatch *p_transparent = nullptr, const Vector3 &p_camera_position = {}, const Vector3 &p_camera_forward = {});
	bool bind_draw(RD::DrawListID p_list, const DrawItem &p_item, RID &r_material_set);

	template <typename T>
	void draw_range(RD::DrawListID p_list, uint32_t p_begin, uint32_t p_end, T &r_constants, RenderingServerTypes::RenderInfo *p_render_info = nullptr) {
		RD *rd = RD::get_singleton();
		RID bound_material_set;
		int *info = p_render_info ? p_render_info->info[pass == SHADOW ? RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW : RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE] : nullptr;
		for (uint32_t i = p_begin; i < p_end; i++) {
			const DrawItem &item = draws[i];
			if (bind_draw(p_list, item, bound_material_set)) {
				r_constants.draw_id = item.instance_id;
				rd->draw_list_set_push_constant(p_list, &r_constants, sizeof(T));
				rd->draw_list_draw(p_list, item.index_array.is_valid());
				if (info) {
					info[RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] += 1;
					info[RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] += int(item.primitive_count);
					info[RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] += 1;
				}
			}
		}
	}

public:
	bool prepare();
	void clear();
	bool has_draws() const { return !draws.is_empty(); }
};

} // namespace RendererSceneRenderImplementation
