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

#include "yutrel_shader.h"

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

namespace RendererSceneRenderImplementation {

struct YutrelFrameData {
	float view_projection[4][4] = {};
	float inv_view_projection[4][4] = {};
	float camera_position[4] = {};
	float camera_forward[4] = {};
	float viewport_origin[2] = {};
	float inv_viewport_size[2] = {};
};

static_assert(sizeof(YutrelFrameData) == 176);
static_assert(offsetof(YutrelFrameData, inv_view_projection) == 64);
static_assert(offsetof(YutrelFrameData, camera_position) == 128);
static_assert(offsetof(YutrelFrameData, camera_forward) == 144);
static_assert(offsetof(YutrelFrameData, viewport_origin) == 160);

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
	RID path_tracer_accum;
	RID gbuffer_a;
	RID gbuffer_b;
	RID gbuffer_c;
	RID shadow_mask;
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
		// Stay within 32-bit RD buffer sizes while accommodating large RT vertex arenas (e.g., Sponza + addons).
		constexpr uint64_t limit = uint64_t(1024) * 1024 * 1024;
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

inline bool refresh_shader(YutrelShader &p_shader, const char *p_path) {
	const uint64_t version = p_shader.get_version();
	if (!p_shader.is_loaded()) {
		p_shader.load(p_path);
	}
	p_shader.poll_hot_reload();
	return p_shader.get_version() != version;
}

inline void draw_fullscreen(Span<char> p_label, const Color &p_color, const Rect2i &p_region, RID p_framebuffer, PipelineCacheRD &p_pipeline, RID p_uniform_set, const void *p_constants = nullptr, uint32_t p_constants_size = 0, bool p_clear = false) {
	RD *rd = RD::get_singleton();
	const RID pipeline = p_pipeline.get_render_pipeline(RD::INVALID_FORMAT_ID, rd->framebuffer_get_format(p_framebuffer));
	ERR_FAIL_COND(pipeline.is_null() || p_uniform_set.is_null());

	rd->draw_command_begin_label(p_label, p_color);
	const Vector<Color> clear_colors = p_clear ? Vector<Color>{ Color(0, 0, 0, 0) } : Vector<Color>();
	const RD::DrawListID list = rd->draw_list_begin(p_framebuffer, p_clear ? RD::DRAW_CLEAR_COLOR_0 : RD::DRAW_DEFAULT_ALL, clear_colors, 0.0f, 0, p_region);
	rd->draw_list_bind_render_pipeline(list, pipeline);
	rd->draw_list_bind_uniform_set(list, p_uniform_set, 0);
	if (p_constants) {
		rd->draw_list_set_push_constant(list, p_constants, p_constants_size);
	}
	rd->draw_list_draw(list, false, 1, 3);
	rd->draw_list_end();
	rd->draw_command_end_label();
}

} // namespace RendererSceneRenderImplementation
