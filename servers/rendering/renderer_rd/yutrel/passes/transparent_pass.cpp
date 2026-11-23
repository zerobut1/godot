/**************************************************************************/
/*  transparent_pass.cpp                                                  */
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

#include "transparent_pass.h"

#include "../yutrel_rt_scene.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

bool TransparentPass::_upload_lights(const LocalVector<DirectionalLightItem> &p_lights, const YutrelRayTracingScene *p_rt_scene) {
	struct TransparentDirectionalLight {
		float direction[3] = {};
		float shadow_opacity = 0;
		float radiance[3] = {};
		uint32_t shadow_index = UINT32_MAX;
	};
	static_assert(sizeof(TransparentDirectionalLight) == 32);

	LocalVector<TransparentDirectionalLight> lights;
	LocalVector<CSMShadowData> shadows;
	const bool trace_shadows = p_rt_scene && p_rt_scene->has_tlas();
	for (const DirectionalLightItem &item : p_lights) {
		TransparentDirectionalLight light;
		memcpy(light.direction, item.params.direction, sizeof(light.direction));
		memcpy(light.radiance, item.params.radiance, sizeof(light.radiance));
		light.shadow_opacity = trace_shadows && item.shadow_enabled ? item.shadow_opacity : 0;
		if (!p_rt_scene && item.shadow.cascade_count > 0) {
			light.shadow_index = shadows.size();
			shadows.push_back(item.shadow);
		}
		lights.push_back(light);
	}
	if (lights.is_empty()) {
		lights.push_back({});
	}
	ERR_FAIL_COND_V(!light_buffer.upload(lights, "Yutrel Transparent Lights"), false);
	if (p_rt_scene) {
		return true;
	}
	if (shadows.is_empty()) {
		shadows.push_back({});
	}
	return shadow_buffer.upload(shadows, "Yutrel Transparent CSM");
}

RID TransparentPass::_get_csm_set(RID p_shadow_array, RID p_comparison_sampler) {
	RD *rd = RD::get_singleton();
	if (p_shadow_array.is_null()) {
		if (fallback_depth.is_null()) {
			RD::TextureFormat format;
			format.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
			format.format = RD::DATA_FORMAT_D32_SFLOAT;
			format.width = format.height = format.array_layers = 1;
			format.usage_bits = RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
			fallback_depth = rd->texture_create(format, {});
			ERR_FAIL_COND_V(fallback_depth.is_null(), RID());
			rd->set_resource_name(fallback_depth, "Yutrel Empty CSM Array");
		}
		p_shadow_array = fallback_depth;
	}
	return UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 2,
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, shadow_buffer.get_buffer()),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_shadow_array),
			RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 2, p_comparison_sampler));
}

void TransparentPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const LocalVector<DirectionalLightItem> &p_lights,
		YutrelRayTracingScene *p_rt_scene, RID p_shadow_array, RID p_comparison_sampler, RID p_dfg_lut, const IBLPass &p_ibl, bool p_has_ibl, const YutrelSkyParams &p_sky, const DDGIBindings &p_ddgi,
		RenderingServerTypes::RenderInfo *p_render_info) {
	ERR_FAIL_COND(p_rt_scene && !prepare_shader(rt_shader, rt_pipelines, "transparent_rt.slang"));
	ERR_FAIL_COND(!upload_geometry() || !_upload_lights(p_lights, p_rt_scene));
	const RID active_shader = p_rt_scene ? rt_shader.get_shader() : shader.get_shader();
	const RID shadow_set = p_rt_scene ? p_rt_scene->get_uniform_set(active_shader, 2) : _get_csm_set(p_shadow_array, p_comparison_sampler);
	ERR_FAIL_COND(shadow_set.is_null());
	RD *rd = RD::get_singleton();
	if (!p_has_ibl && fallback_sh.is_null()) {
		fallback_sh = rd->storage_buffer_create(9 * 4 * sizeof(float));
		ERR_FAIL_COND(fallback_sh.is_null());
		rd->set_resource_name(fallback_sh, "Yutrel Empty SH");
	}
	using TextureStorage = RendererRD::TextureStorage;
	const RID cube = p_has_ibl ? p_ibl.get_prefilter_cubemap() : TextureStorage::get_singleton()->texture_rd_get_default(TextureStorage::DEFAULT_RD_TEXTURE_CUBEMAP_BLACK);
	const RID linear = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	using Uniform = RD::Uniform;
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(active_shader, 0,
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, material_buffer.get_buffer()),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, light_buffer.get_buffer()),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_dfg_lut),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 8, linear),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 9, cube),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 10, p_has_ibl ? p_ibl.get_cube_sampler() : linear),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 11, p_has_ibl ? p_ibl.get_sh_buffer() : fallback_sh),
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 12, p_ddgi.volume),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 13, p_ddgi.irradiance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 14, p_ddgi.distance),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 15, p_ddgi.probe_data));
	const RID framebuffer = FramebufferCacheRD::get_singleton()->get_cache(p_targets.scene_color, p_targets.scene_depth);
	ERR_FAIL_COND(set.is_null() || framebuffer.is_null());

	draws.sort_custom<BackToFront>();
	resolve_draws(rd->framebuffer_get_format(framebuffer), p_rt_scene ? rt_pipelines : pipelines);
	struct {
		uint32_t draw_id;
		uint32_t light_count;
		uint32_t has_ibl;
		YutrelSkyParams sky;
		float prefilter_max_mip;
		uint32_t pad;
	} draw = { 0, p_lights.size(), uint32_t(p_has_ibl), p_sky, IBLPass::get_prefilter_max_mip(), 0 };
	static_assert(sizeof(draw) == 32);

	rd->draw_command_begin_label("Yutrel TransparentPass", Color(0.5, 0.7, 0.9));
	const RD::DrawListID list = rd->draw_list_begin(framebuffer, RD::DRAW_DEFAULT_ALL, {}, 0.0f, 0, p_targets.region, RDD::BreadcrumbMarker::TRANSPARENT_PASS);
	rd->draw_list_bind_uniform_set(list, set, 0);
	rd->draw_list_bind_uniform_set(list, shadow_set, 2);
	draw_range(list, 0, draws.size(), draw, p_render_info);
	rd->draw_list_end();
	rd->draw_command_end_label();
}

TransparentPass::~TransparentPass() {
	RD *rd = RD::get_singleton();
	if (fallback_sh.is_valid()) {
		rd->free_rid(fallback_sh);
	}
	if (fallback_depth.is_valid()) {
		rd->free_rid(fallback_depth);
	}
}

} // namespace RendererSceneRenderImplementation
