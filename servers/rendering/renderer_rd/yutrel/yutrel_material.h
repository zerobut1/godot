/**************************************************************************/
/*  yutrel_material.h                                                     */
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

class RenderGeometryInstanceBase;

namespace RendererSceneRenderImplementation {

struct YutrelMaterialData {
	enum Flags : uint32_t {
		HAS_NORMAL_MAP = 1 << 0,
		ALPHA_SCISSOR = 1 << 1,
		DOUBLE_SIDED = 1 << 2,
		VERTEX_COLOR = 1 << 3,
		SRGB_VERTEX_COLOR = 1 << 4,
		EMISSION_OP_MULTIPLY = 1 << 5,
		HAS_EMISSION_TEXTURE = 1 << 6,
		UNLIT = 1 << 7,
		CULL_FRONT = 1 << 8,
	};

	float albedo[4] = { 1, 1, 1, 1 };
	float emission[3] = {};
	float emission_energy = 0;
	float uv_transform[4] = { 1, 1, 0, 0 };
	float roughness_channel[4] = { 1, 0, 0, 0 };
	float metallic_channel[4] = { 1, 0, 0, 0 };
	float ao_channel[4] = { 1, 0, 0, 0 };
	float roughness = 1;
	float metallic = 0;
	float specular = 0.5;
	float normal_scale = 1;
	float alpha_scissor_threshold = 0.5;
	uint32_t flags = 0;
	float padding[2] = {};
};

static_assert(sizeof(YutrelMaterialData) == 128);
static_assert(offsetof(YutrelMaterialData, emission) == 16);
static_assert(offsetof(YutrelMaterialData, emission_energy) == 28);
static_assert(offsetof(YutrelMaterialData, uv_transform) == 32);
static_assert(offsetof(YutrelMaterialData, roughness_channel) == 48);
static_assert(offsetof(YutrelMaterialData, metallic_channel) == 64);
static_assert(offsetof(YutrelMaterialData, ao_channel) == 80);
static_assert(offsetof(YutrelMaterialData, roughness) == 96);
static_assert(offsetof(YutrelMaterialData, alpha_scissor_threshold) == 112);
static_assert(offsetof(YutrelMaterialData, flags) == 116);

class YutrelMaterialBridge {
public:
	enum TextureSlot {
		ALBEDO,
		NORMAL,
		ROUGHNESS,
		METALLIC,
		AO,
		EMISSION,
		TEXTURE_COUNT,
	};

	struct Material {
		YutrelMaterialData data;
		RID textures[TEXTURE_COUNT];
	};

#ifdef TOOLS_ENABLED
	enum GizmoFlags : uint32_t {
		VERTEX_COLOR = 1 << 0,
		SRGB_VERTEX_COLOR = 1 << 1,
		BILLBOARD = 1 << 2,
		FIXED_SIZE = 1 << 3,
		GRID = 1 << 4,
		AXIS_RING = 1 << 5,
		VIEW_RING = 1 << 6,
		ORIGIN_AXIS = 1 << 7,
		POINT_SPRITE = 1 << 8,
	};

	struct GizmoStyle {
		uint32_t flags = 0;
		bool on_top = false;
		bool alpha_scissor = false;
		bool supported = false;
	};

	static const GizmoStyle *get_gizmo_style(RID p_material, int32_t &r_priority);
#endif

	YutrelMaterialBridge();
	~YutrelMaterialBridge();

	static RID get_surface_material(const RenderGeometryInstanceBase &p_instance, RID p_mesh, int p_surface_index);
	RID get_white_texture();
	bool get_material(RID p_material, Material &r_material);
	RID get_uniform_set(RID p_shader, const Material &p_material) const;

private:
	RID white_texture;
	RID flat_normal_texture;
};

} // namespace RendererSceneRenderImplementation
