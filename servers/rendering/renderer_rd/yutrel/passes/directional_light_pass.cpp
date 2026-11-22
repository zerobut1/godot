/**************************************************************************/
/*  directional_light_pass.cpp                                            */
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

#include "directional_light_pass.h"

#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

DirectionalLightPass::~DirectionalLightPass() {
	if (dfg_lut.is_valid()) {
		RD::get_singleton()->free_rid(dfg_lut);
	}
}

bool DirectionalLightPass::prepare() {
	if (refresh_shader(lighting_shader, "directional_light.slang")) {
		RD::PipelineColorBlendState blend = RD::PipelineColorBlendState::create_disabled();
		auto &attachment = blend.attachments.write[0];
		attachment.enable_blend = true;
		attachment.src_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.write_a = false;
		lighting_pipeline.setup(lighting_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, blend);
	}
	return lighting_shader.get_shader().is_valid();
}

void DirectionalLightPass::initialize_dfg_lut() {
	dfg_shader.load("dfg_lut.slang");
	if (dfg_shader.get_shader().is_valid()) {
		_bake_dfg_lut();
	}
}

bool DirectionalLightPass::prepare_dfg_lut() {
	dfg_shader.poll_hot_reload();
	if (dfg_shader.get_shader().is_valid() && dfg_baked_version != dfg_shader.get_version()) {
		_bake_dfg_lut();
	}
	return dfg_baked_version != 0;
}

bool DirectionalLightPass::_bake_dfg_lut() {
	RD *rd = RD::get_singleton();
	constexpr uint32_t size = 64;
	if (dfg_lut.is_null()) {
		RD::TextureFormat format;
		format.width = size;
		format.height = size;
		format.format = RD::DATA_FORMAT_R16G16_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		dfg_lut = rd->texture_create(format, {});
		ERR_FAIL_COND_V(dfg_lut.is_null(), false);
		rd->set_resource_name(dfg_lut, "Yutrel DFG LUT");
	}

	const RID shader = dfg_shader.get_shader();
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(shader, 0, RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, dfg_lut));
	ERR_FAIL_COND_V(set.is_null(), false);
	const RID pipeline = rd->compute_pipeline_create(shader);
	ERR_FAIL_COND_V(pipeline.is_null(), false);

	rd->draw_command_begin_label("Yutrel DFG LUT", Color(0.7, 0.3, 0.9));
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, pipeline);
	rd->compute_list_bind_uniform_set(list, set, 0);
	rd->compute_list_dispatch_threads(list, size, size, 1);
	rd->compute_list_end();
	rd->draw_command_end_label();
	rd->free_rid(pipeline);
	dfg_baked_version = dfg_shader.get_version();
	return true;
}

LocalVector<DirectionalLightItem> DirectionalLightPass::collect(const RenderDataRD &p_data) {
	LocalVector<DirectionalLightItem> items;
	auto *lights = RendererRD::LightStorage::get_singleton();
	if (p_data.lights) {
		for (uint32_t i = 0; i < p_data.lights->size(); i++) {
			const RID instance = (*p_data.lights)[i];
			const RID light = lights->light_instance_get_base_light(instance);
			if (lights->light_get_type(light) != RSE::LIGHT_DIRECTIONAL || lights->light_directional_get_sky_mode(light) == RSE::LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY) {
				continue;
			}
			const uint32_t cull_mask = lights->light_get_cull_mask(light);
			if (!(cull_mask & p_data.scene_data->camera_visible_layers)) {
				continue;
			}
			const Vector3 direction = lights->light_instance_get_base_transform(instance).basis.get_column(2).normalized();
			const float energy = lights->light_get_param(light, RSE::LIGHT_PARAM_ENERGY) * (lights->light_is_negative(light) ? -1.0f : 1.0f);
			const Color radiance = lights->light_get_color(light).srgb_to_linear() * energy;
			DirectionalLightItem item;
			item.instance = instance;
			for (int axis = 0; axis < 3; axis++) {
				item.params.direction[axis] = direction[axis];
				item.params.radiance[axis] = radiance[axis];
			}
			items.push_back(item);
		}
	}
	return items;
}

void DirectionalLightPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const DirectionalLightParams &p_light, RID p_mask, bool p_clear) {
	const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID light_set = UniformSetCacheRD::get_singleton()->get_cache(lighting_shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_a),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_b),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.gbuffer_c),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.scene_depth),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, dfg_lut),
			RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 6, sampler),
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, p_mask));
	draw_fullscreen("Yutrel DirectionalLightPass", Color(0.9, 0.8, 0.3), p_targets.region, p_targets.lighting_framebuffer, lighting_pipeline, light_set, &p_light, sizeof(p_light), p_clear);
}

} // namespace RendererSceneRenderImplementation
