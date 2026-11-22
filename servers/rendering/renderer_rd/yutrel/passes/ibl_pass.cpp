/**************************************************************************/
/*  ibl_pass.cpp                                                          */
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

#include "ibl_pass.h"

#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

namespace {

using Uniform = RD::Uniform;

struct EquirectParams {
	float color[3];
	uint32_t use_panorama;
	uint32_t face;
	uint32_t size;
};

struct PrefilterParams {
	uint32_t face;
	uint32_t size;
	float mip_fraction;
	float source_texel_solid_angle;
};

struct SHParams {
	uint32_t reduce;
	uint32_t face_size;
};

struct IBLParams {
	YutrelSkyParams sky;
	float max_mip;
};

static_assert(sizeof(EquirectParams) == 24);
static_assert(sizeof(PrefilterParams) == 16);
static_assert(sizeof(SHParams) == 8);
static_assert(sizeof(IBLParams) == 16);

template <typename Params>
bool dispatch(RID p_pipeline, RID p_set, const Params &p_params, uint32_t p_x, uint32_t p_y, uint32_t p_z = 1) {
	ERR_FAIL_COND_V(p_set.is_null(), false);
	RD *rd = RD::get_singleton();
	// Separate lists let RD track dependencies between mip writes and subsequent reads.
	const RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, p_pipeline);
	rd->compute_list_bind_uniform_set(list, p_set, 0);
	rd->compute_list_set_push_constant(list, &p_params, sizeof(p_params));
	rd->compute_list_dispatch_threads(list, p_x, p_y, p_z);
	rd->compute_list_end();
	return true;
}

} // namespace

IBLPass::BakeShader::~BakeShader() {
	if (pipeline.is_valid()) {
		RD::get_singleton()->free_rid(pipeline);
	}
}

bool IBLPass::BakeShader::prepare(const char *p_path) {
	if (refresh_shader(shader, p_path)) {
		// YutrelShader frees the old shader and its dependent compute pipeline.
		pipeline = RID();
	}
	if (pipeline.is_null() && shader.get_shader().is_valid()) {
		pipeline = RD::get_singleton()->compute_pipeline_create(shader.get_shader());
	}
	return pipeline.is_valid();
}

IBLPass::Cube::~Cube() {
	if (texture.is_valid()) {
		// RD also frees all dependent face views.
		RD::get_singleton()->free_rid(texture);
	}
}

bool IBLPass::Cube::initialize(uint32_t p_size, uint32_t p_mips, const char *p_name) {
	RD *rd = RD::get_singleton();
	if (texture.is_null()) {
		RD::TextureFormat format;
		format.width = p_size;
		format.height = p_size;
		format.array_layers = 6;
		format.mipmaps = p_mips;
		format.texture_type = RD::TEXTURE_TYPE_CUBE;
		format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		texture = rd->texture_create(format, {});
		ERR_FAIL_COND_V(texture.is_null(), false);
		rd->set_resource_name(texture, p_name);
	}

	// RenderingDevice only allows 2D_ARRAY slices of 2D_ARRAY parents. Each 2D
	// face view writes into the same cube image, with no copy to a second texture.
	while (faces.size() < p_mips * 6) {
		const uint32_t index = faces.size();
		const RID view = rd->texture_create_shared_from_slice({}, texture, index % 6, index / 6);
		ERR_FAIL_COND_V(view.is_null(), false);
		faces.push_back(view);
	}
	return true;
}

IBLPass::~IBLPass() {
	RD *rd = RD::get_singleton();
	for (const RID rid : { sh_partial_sums, sh_buffer, sh_source_view, cube_sampler, panorama_sampler }) {
		if (rid.is_valid()) {
			rd->free_rid(rid);
		}
	}
}

void IBLPass::prepare() {
	if (refresh_shader(lighting_shader, "ibl.slang")) {
		RD::PipelineColorBlendState blend = RD::PipelineColorBlendState::create_disabled();
		auto &attachment = blend.attachments.write[0];
		attachment.enable_blend = true;
		attachment.src_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
		attachment.write_a = false;
		lighting_pipeline.setup(lighting_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, blend);
	}
}

bool IBLPass::_initialize_resources() {
	RD *rd = RD::get_singleton();
	if (!source.initialize(SOURCE_SIZE, SOURCE_MIPS, "Yutrel IBL source cubemap") ||
			!filtered.initialize(PREFILTER_SIZE, PREFILTER_MIPS, "Yutrel IBL prefiltered cubemap")) {
		return false;
	}

	if (cube_sampler.is_null()) {
		RD::SamplerState state;
		state.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		state.min_filter = RD::SAMPLER_FILTER_LINEAR;
		state.mip_filter = RD::SAMPLER_FILTER_LINEAR;
		cube_sampler = rd->sampler_create(state);
		ERR_FAIL_COND_V(cube_sampler.is_null(), false);
	}
	if (panorama_sampler.is_null()) {
		RD::SamplerState state;
		state.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		state.min_filter = RD::SAMPLER_FILTER_LINEAR;
		state.repeat_u = RD::SAMPLER_REPEAT_MODE_REPEAT;
		panorama_sampler = rd->sampler_create(state);
		ERR_FAIL_COND_V(panorama_sampler.is_null(), false);
	}
	if (sh_source_view.is_null()) {
		sh_source_view = rd->texture_create_shared_from_slice({}, source.texture, 0, SH_SOURCE_MIP, 1, RD::TEXTURE_SLICE_CUBEMAP);
		ERR_FAIL_COND_V(sh_source_view.is_null(), false);
	}
	constexpr uint32_t sh_bytes = 9 * 4 * sizeof(float);
	if (sh_buffer.is_null()) {
		sh_buffer = rd->storage_buffer_create(sh_bytes);
		ERR_FAIL_COND_V(sh_buffer.is_null(), false);
		rd->set_resource_name(sh_buffer, "Yutrel IBL SH3");
	}
	if (sh_partial_sums.is_null()) {
		constexpr uint32_t tiles_per_row = (SOURCE_SIZE >> SH_SOURCE_MIP) / 8;
		sh_partial_sums = rd->storage_buffer_create(sh_bytes * 6 * tiles_per_row * tiles_per_row);
		ERR_FAIL_COND_V(sh_partial_sums.is_null(), false);
	}
	return true;
}

bool IBLPass::prepare_environment(const YutrelEnvironmentSettings &p_environment) {
	// Poll all bake shaders independently, including imported Slang modules.
	const bool equirect_ready = equirect.prepare("ibl_equirect_to_cube.slang");
	const bool downsample_ready = downsample.prepare("ibl_downsample_cube.slang");
	const bool prefilter_ready = prefilter.prepare("ibl_prefilter_specular.slang");
	const bool sh_ready = convolve_sh.prepare("ibl_convolve_sh.slang");
	if (!equirect_ready || !downsample_ready || !prefilter_ready || !sh_ready) {
		return false;
	}

	RID panorama;
	if (p_environment.sky_mode == SkyMode::HDRI) {
		// SDR color textures have a hardware sRGB view. Floating point HDR textures
		// use the original format and are already linear.
		panorama = RendererRD::TextureStorage::get_singleton()->texture_get_rd_texture(p_environment.sky_texture_rid, true);
		RD *rd = RD::get_singleton();
		if (panorama.is_valid() && (!rd->texture_is_valid(panorama) || rd->texture_get_format(panorama).texture_type != RD::TEXTURE_TYPE_2D)) {
			panorama = RID();
		}
	}

	const BakeKey key = {
		p_environment.sky_mode,
		p_environment.linear_sky_color,
		panorama,
		{ equirect.shader.get_version(), downsample.shader.get_version(), prefilter.shader.get_version(), convolve_sh.shader.get_version() },
	};
	if (baked && key == baked_key) {
		return true;
	}
	if (!_initialize_resources()) {
		return false;
	}

	RD *rd = RD::get_singleton();
	rd->draw_command_begin_label("Yutrel IBL Bake", Color(0.35, 0.6, 0.85));
	baked = _bake(key);
	rd->draw_command_end_label();
	if (baked) {
		baked_key = key;
		print_verbose("Yutrel: baked IBL.");
	}
	return baked;
}

bool IBLPass::_bake(const BakeKey &p_key) {
	auto *sets = UniformSetCacheRD::get_singleton();
	const bool use_panorama = p_key.panorama.is_valid();
	const RID panorama = use_panorama ? p_key.panorama : RendererRD::TextureStorage::get_singleton()->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_WHITE);
	const Color color = p_key.color;
	for (uint32_t face = 0; face < 6; face++) {
		const RID set = sets->get_cache(equirect.shader.get_shader(), 0,
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, panorama),
				Uniform(RD::UNIFORM_TYPE_SAMPLER, 1, panorama_sampler),
				Uniform(RD::UNIFORM_TYPE_IMAGE, 2, source.face(0, face)));
		const EquirectParams params = { { color.r, color.g, color.b }, use_panorama, face, SOURCE_SIZE };
		if (!dispatch(equirect.pipeline, set, params, SOURCE_SIZE, SOURCE_SIZE)) {
			return false;
		}
	}

	for (uint32_t mip = 1; mip < SOURCE_MIPS; mip++) {
		const uint32_t size = SOURCE_SIZE >> mip;
		for (uint32_t face = 0; face < 6; face++) {
			const RID set = sets->get_cache(downsample.shader.get_shader(), 0,
					Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, source.face(mip - 1, face)),
					Uniform(RD::UNIFORM_TYPE_IMAGE, 1, source.face(mip, face)));
			if (!dispatch(downsample.pipeline, set, size, size, size)) {
				return false;
			}
		}
	}

	constexpr float texel_solid_angle = 4.0f * Math::PI / (6.0f * SOURCE_SIZE * SOURCE_SIZE);
	for (uint32_t mip = 0; mip < PREFILTER_MIPS; mip++) {
		const uint32_t size = PREFILTER_SIZE >> mip;
		const float mip_fraction = float(mip) / (PREFILTER_MIPS - 1);
		for (uint32_t face = 0; face < 6; face++) {
			const RID set = sets->get_cache(prefilter.shader.get_shader(), 0,
					Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, source.texture),
					Uniform(RD::UNIFORM_TYPE_SAMPLER, 1, cube_sampler),
					Uniform(RD::UNIFORM_TYPE_IMAGE, 2, filtered.face(mip, face)));
			const PrefilterParams params = { face, size, mip_fraction, texel_solid_angle };
			if (!dispatch(prefilter.pipeline, set, params, size, size)) {
				return false;
			}
		}
	}

	const RID point = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID set = sets->get_cache(convolve_sh.shader.get_shader(), 0,
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, sh_source_view),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 1, point),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 2, sh_partial_sums),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, sh_buffer));
	constexpr uint32_t sh_size = SOURCE_SIZE >> SH_SOURCE_MIP;
	return dispatch(convolve_sh.pipeline, set, SHParams{ 0, sh_size }, sh_size, sh_size, 6) &&
			dispatch(convolve_sh.pipeline, set, SHParams{ 1, sh_size }, 9 * 8, 8);
}

void IBLPass::render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_dfg_lut, const YutrelSkyParams &p_sky) {
	if (!baked || lighting_shader.get_shader().is_null()) {
		return;
	}

	const RID linear_clamp = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(lighting_shader.get_shader(), 0,
			Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_a),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.gbuffer_b),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.gbuffer_c),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_targets.scene_depth),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, p_dfg_lut),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 6, linear_clamp),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 7, filtered.texture),
			Uniform(RD::UNIFORM_TYPE_SAMPLER, 8, cube_sampler),
			Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 9, sh_buffer));
	const IBLParams params = { p_sky, get_prefilter_max_mip() };
	draw_fullscreen("Yutrel IBLPass", Color(0.25, 0.7, 0.85), p_targets.region, p_targets.lighting_framebuffer, lighting_pipeline, set, &params, sizeof(params));
}

} // namespace RendererSceneRenderImplementation
