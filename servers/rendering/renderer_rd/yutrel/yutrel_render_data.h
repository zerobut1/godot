/**************************************************************************/
/*  yutrel_render_data.h                                                 */
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

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

namespace RendererSceneRenderImplementation {

struct YutrelDirectionalLight {
	float direction[4] = {};
	float radiance[4] = {};
};

static_assert(sizeof(YutrelDirectionalLight) == 32);
static_assert(offsetof(YutrelDirectionalLight, radiance) == 16);

struct YutrelFrameData {
	float view_projection[4][4] = {};
	float inv_view_projection[4][4] = {};
	float camera_position[4] = {};
	float camera_forward[4] = {};
	float viewport_origin[2] = {};
	float inv_viewport_size[2] = {};
	uint32_t light_count = 0;
	float padding0 = 0;
	float padding1[2] = {};
	YutrelDirectionalLight lights[4];
};

static_assert(sizeof(YutrelFrameData) == 320);
static_assert(offsetof(YutrelFrameData, inv_view_projection) == 64);
static_assert(offsetof(YutrelFrameData, camera_position) == 128);
static_assert(offsetof(YutrelFrameData, camera_forward) == 144);
static_assert(offsetof(YutrelFrameData, viewport_origin) == 160);
static_assert(offsetof(YutrelFrameData, light_count) == 176);
static_assert(offsetof(YutrelFrameData, lights) == 192);

struct YutrelInstanceData {
	enum Flags : uint32_t {
		COMPRESSED = 1,
		HAS_TANGENT = 2,
		HAS_COLOR = 4,
	};

	float model[3][4] = {};
	float normal[3][4] = {};
	float aabb_position[3] = {};
	uint32_t flags = 0;
	float aabb_size[3] = {};
	uint32_t padding0 = 0;
	float uv_scale[2] = {};
	uint32_t material_id = 0;
	uint32_t padding1 = 0;

	void set_transform(const Transform3D &p_transform, const Basis &p_normal) {
		RendererRD::MaterialStorage::store_transform_transposed_3x4(p_transform, &model[0][0]);
		for (int row = 0; row < 3; row++) {
			for (int column = 0; column < 3; column++) {
				normal[row][column] = p_normal.rows[row][column];
			}
		}
	}

	void set_compressed_aabb(const AABB &p_aabb) {
		flags |= COMPRESSED;
		for (int axis = 0; axis < 3; axis++) {
			aabb_position[axis] = p_aabb.position[axis];
			aabb_size[axis] = p_aabb.size[axis];
		}
	}
};

static_assert(sizeof(YutrelInstanceData) == 144);
static_assert(offsetof(YutrelInstanceData, normal) == 48);
static_assert(offsetof(YutrelInstanceData, aabb_position) == 96);
static_assert(offsetof(YutrelInstanceData, flags) == 108);
static_assert(offsetof(YutrelInstanceData, aabb_size) == 112);
static_assert(offsetof(YutrelInstanceData, uv_scale) == 128);
static_assert(offsetof(YutrelInstanceData, material_id) == 136);

struct YutrelPipelineTargets {
	RID render_target;
	RID scene_color;
	RID scene_depth;
	RID gbuffer_a;
	RID gbuffer_b;
	RID gbuffer_c;
	RID output_color;
	RID base_framebuffer;
	RID lighting_framebuffer;
	RID output_framebuffer;
	Rect2i region;
	bool output_linear = false;
};

class YutrelStorageBuffer {
	RID buffer;
	uint32_t capacity = 0;

public:
	~YutrelStorageBuffer() {
		if (buffer.is_valid()) {
			RD::get_singleton()->free_rid(buffer);
		}
	}

	RID get_buffer() const { return buffer; }

	template <typename T>
	bool upload(const LocalVector<T> &p_data, const char *p_name) {
		if (p_data.is_empty()) {
			return true;
		}

		const uint64_t bytes = uint64_t(p_data.size()) * sizeof(T);
		RD *rd = RD::get_singleton();
		// Vulkan guarantees at least this range for a storage buffer descriptor.
		constexpr uint64_t limit = 128 * 1024 * 1024;
		ERR_FAIL_COND_V(bytes > limit, false);

		if (bytes > capacity) {
			const uint32_t new_capacity = MIN(MAX(bytes, uint64_t(capacity) * 2), limit);
			const RID new_buffer = rd->storage_buffer_create(new_capacity);
			ERR_FAIL_COND_V(new_buffer.is_null(), false);
			rd->set_resource_name(new_buffer, p_name);
			if (buffer.is_valid()) {
				// UniformSetCacheRD removes dependent sets when their buffer is freed.
				rd->free_rid(buffer);
			}
			buffer = new_buffer;
			capacity = new_capacity;
		}

		return rd->buffer_update(buffer, 0, uint32_t(bytes), p_data.ptr()) == OK;
	}
};

} // namespace RendererSceneRenderImplementation
