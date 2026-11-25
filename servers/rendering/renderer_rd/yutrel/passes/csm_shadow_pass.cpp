/**************************************************************************/
/*  csm_shadow_pass.cpp                                                   */
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

#include "csm_shadow_pass.h"

#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

CSMShadowPass::~CSMShadowPass() {
	if (depth_array.is_valid()) {
		RD::get_singleton()->free_rid(depth_array);
	}
}

bool CSMShadowPass::_ensure_array(uint32_t p_resolution, uint32_t p_layers) {
	if (resolution == p_resolution && p_layers <= framebuffers.size()) {
		return true;
	}
	RD *rd = RD::get_singleton();
	RD::TextureFormat format;
	format.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	format.format = RD::DATA_FORMAT_D32_SFLOAT;
	format.width = format.height = p_resolution;
	format.array_layers = p_layers;
	format.usage_bits = RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	const RID array = rd->texture_create(format, {});
	ERR_FAIL_COND_V(array.is_null(), false);
	rd->set_resource_name(array, "Yutrel CSM depth array");

	LocalVector<RID> new_framebuffers;
	for (uint32_t i = 0; i < p_layers; i++) {
		const RID slice = rd->texture_create_shared_from_slice({}, array, i, 0);
		const RID fb = slice.is_valid() ? FramebufferCacheRD::get_singleton()->get_cache(slice) : RID();
		if (fb.is_null()) {
			rd->free_rid(array);
			return false;
		}
		new_framebuffers.push_back(fb);
	}
	if (depth_array.is_valid()) {
		// RD frees dependent views, framebuffers and descriptor sets.
		rd->free_rid(depth_array);
	}
	framebuffers = std::move(new_framebuffers);
	depth_array = array;
	resolution = p_resolution;
	return true;
}

bool CSMShadowPass::render(const RenderDataRD &p_data, LocalVector<DirectionalLightItem> &r_lights, uint32_t p_resolution, YutrelMaterialBridge &p_bridge) {
	auto *storage = RendererRD::LightStorage::get_singleton();
	uint32_t layers = 0;
	clear();
	LocalVector<uint32_t> draw_ends;
	Projection correction;
	correction.set_depth_correction(true, true, true);
	for (DirectionalLightItem &item : r_lights) {
		const RID base = storage->light_instance_get_base_light(item.instance);
		if (!item.shadow_enabled) {
			continue;
		}
		const auto mode = storage->light_directional_get_shadow_mode(base);
		const int count = mode == RSE::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL ? 1 : (mode == RSE::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS ? 2 : 4);
		int shadow_jobs[4] = {};
		int present = 0;
		for (int i = 0; i < p_data.render_shadow_count; i++) {
			const auto &job = p_data.render_shadows[i];
			if (job.light == item.instance && job.pass >= 0 && job.pass < count) {
				shadow_jobs[job.pass] = i;
				present |= 1 << job.pass;
			}
		}
		// A viewport with shadows disabled submits no jobs. Never sample stale layers.
		if (present != (1 << count) - 1) {
			continue;
		}
		auto &shadow = item.shadow;
		item.params.has_shadow = 1;
		shadow.first_layer = layers;
		shadow.cascade_count = count;
		shadow.inv_resolution = 1.0f / p_resolution;
		shadow.normal_bias = storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_NORMAL_BIAS);
		shadow.blur_radius = storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_BLUR);
		shadow.opacity = item.shadow_opacity;
		shadow.blend_splits = storage->light_directional_get_blend_splits(base);
		shadow.camera_near = p_data.scene_data->z_near;
		for (int i = 0; i < count; i++) {
			const Projection camera = storage->light_instance_get_shadow_camera(item.instance, i);
			const Transform3D transform = storage->light_instance_get_shadow_transform(item.instance, i);
			// SceneCull supplies GL ortho clip space and the light camera's world transform.
			// Store the corrected matrix once for both rasterization and receiver lookup.
			const Projection vp = correction * camera * Projection(transform.affine_inverse());
			for (int row = 0; row < 4; row++) {
				for (int col = 0; col < 4; col++) {
					shadow.view_projection[i][row][col] = vp.columns[col][row];
				}
			}
			shadow.splits[i] = storage->light_instance_get_directional_shadow_split(item.instance, i);
			shadow.world_texel_size[i] = 2.0f * shadow.inv_resolution / Math::abs(camera.columns[0][0]);
			const float z_range = storage->light_instance_get_shadow_range(item.instance, i);
			shadow.depth_bias[i] = storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_BIAS) * 0.01f * storage->light_instance_get_shadow_bias_scale(item.instance, i) / z_range;

			const auto &casters = p_data.render_shadows[shadow_jobs[i]].instances;
			const bool mirrored = transform.basis.determinant() < 0;
			for (uint32_t j = 0; j < casters.size(); j++) {
				add_geometry(*static_cast<const RenderGeometryInstanceBase *>(casters[j]), mirrored, p_bridge);
			}
			draw_ends.push_back(draws.size());
		}
		const float max_distance = storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_MAX_DISTANCE);
		shadow.max_distance = max_distance > 0 ? MIN(max_distance, shadow.splits[count - 1]) : shadow.splits[count - 1];
		shadow.fade_start = shadow.max_distance * storage->light_get_param(base, RSE::LIGHT_PARAM_SHADOW_FADE_START);
		layers += count;
	}
	if (layers == 0) {
		return true;
	}
	ERR_FAIL_COND_V(!_ensure_array(p_resolution, layers), false);
	const YutrelScopedLabel pass_label("Yutrel CSMShadowPass", Color(0.3, 0.4, 0.7));
	RD *rd = RD::get_singleton();
	RID set;
	if (has_draws()) {
		ERR_FAIL_COND_V(!upload_geometry(), false);
		set = UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 0,
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()),
				RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, material_buffer.get_buffer()));
		ERR_FAIL_COND_V(set.is_null(), false);
		resolve_draws(rd->framebuffer_get_format(framebuffers[0]), pipelines);
	}

	uint32_t begin = 0;
	for (const DirectionalLightItem &item : r_lights) {
		for (uint32_t i = 0; i < item.shadow.cascade_count; i++) {
			const uint32_t layer = item.shadow.first_layer + i;
			const RD::DrawListID list = rd->draw_list_begin(framebuffers[layer], RD::DRAW_CLEAR_DEPTH, {}, 0.0f);
			if (begin < draw_ends[layer]) {
				rd->draw_list_bind_uniform_set(list, set, 0);
				struct {
					float vp[4][4];
					uint32_t draw_id;
				} draw;
				memcpy(draw.vp, item.shadow.view_projection[i], sizeof(draw.vp));
				draw_range(list, begin, draw_ends[layer], material_sets, draw, p_data.render_info);
			}
			rd->draw_list_end();
			begin = draw_ends[layer];
		}
	}
	return true;
}

} // namespace RendererSceneRenderImplementation
