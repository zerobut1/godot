/**************************************************************************/
/*  yutrel_rt_scene.h                                                     */
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

class RenderDataRD;

namespace RendererSceneRenderImplementation {

struct YutrelRTVertex {
	float position[3];
	float u;
	float normal[3];
	float v;
	float tangent[4];
	float color[4];
};
static_assert(sizeof(YutrelRTVertex) == 64);

struct YutrelRTInstance {
	float model[3][4];
	float normal[3][4];
	uint32_t first_surface;
	uint32_t surface_count;
	uint32_t flags;
	uint32_t padding;
};
static_assert(sizeof(YutrelRTInstance) == 112);

struct YutrelRTSurface {
	uint32_t vertex_offset;
	uint32_t index_offset;
	uint32_t material_id;
	uint32_t flags;
};
static_assert(sizeof(YutrelRTSurface) == 16);

struct YutrelRTMaterialTextures {
	uint32_t slots[YutrelMaterialBridge::TEXTURE_COUNT];
	uint32_t padding[2];
};
static_assert(sizeof(YutrelRTMaterialTextures) == 32);

// FNV-1a over initialized, padding-free GPU records and individual scalar fields.
inline uint64_t rt_hash_bytes(uint64_t p_hash, const void *p_data, size_t p_size) {
	const uint8_t *bytes = static_cast<const uint8_t *>(p_data);
	for (size_t i = 0; i < p_size; i++) {
		p_hash = (p_hash ^ bytes[i]) * UINT64_C(1099511628211);
	}
	return p_hash;
}

class YutrelRayTracingScene {
	struct MeshEntry;
	HashMap<RID, MeshEntry *> meshes;
	Vector<RID> arena_blas;
	Vector<RID> textures;
	YutrelStorageBuffer vertices, indices, instances, surfaces, materials, material_textures;
	RID tlas;
	uint32_t capacity = 0;
	bool has_instances = false;
	bool tables_valid = false;
	uint64_t scene_hash = 0;
	RID uniform_set;
	RID set_shader;

	bool _update_arena(const Vector<RID> &p_meshes);
	void _free_set();

public:
	static constexpr uint32_t MAX_TEXTURES = 512;

	static bool is_supported(uint32_t p_pass_textures);
	static RID get_scenario(const RenderDataRD &p_data);

	YutrelRayTracingScene() = default;
	YutrelRayTracingScene(const YutrelRayTracingScene &) = delete;
	YutrelRayTracingScene &operator=(const YutrelRayTracingScene &) = delete;
	~YutrelRayTracingScene();

	// Collect static meshes from the supplied render view. Set 1 is owned by this scene.
	bool update(const RenderDataRD &p_data, YutrelMaterialBridge &p_bridge);
	RID get_uniform_set(RID p_shader);
	uint64_t get_scene_hash() const { return scene_hash; }
	bool has_tlas() const { return has_instances; }
};

} // namespace RendererSceneRenderImplementation
