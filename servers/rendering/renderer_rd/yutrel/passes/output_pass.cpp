/**************************************************************************/
/*  output_pass.cpp                                                       */
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

#include "output_pass.h"

#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

namespace {

constexpr uint32_t LUT_SIZE = 32;

// Mirrors the std140 blocks in color_grading_common.slang.
struct GradingData {
	float temperature, tint, contrast, saturation;
	Color shadows, midtones, highlights;
	uint32_t tone_mapping;
	float strip_size, strip_intensity;
	uint32_t has_grading;
};

struct OutputUniforms {
	GradingData grade;
	uint32_t output_linear, use_auto_exposure, use_lut_3d, debanding;
	float exposure, bloom_intensity, vignette_intensity, vignette_smoothness;
	float origin[2], inv_size[2];
};
static_assert(sizeof(GradingData) == 80 && offsetof(GradingData, tone_mapping) == 64);
static_assert(sizeof(OutputUniforms) == 128 && offsetof(OutputUniforms, origin) == 112);

} // namespace

void OutputPass::LutCache::clear() {
	if (texture.is_valid()) {
		RD::get_singleton()->free_rid(texture);
		texture = RID();
	}
	hash = 0;
}

OutputPass::~OutputPass() {
	if (black_fallback.is_valid()) {
		RD::get_singleton()->free_rid(black_fallback);
	}
	if (uniform_buffer.is_valid()) {
		RD::get_singleton()->free_rid(uniform_buffer);
	}
}

bool OutputPass::prepare() {
	if (refresh_shader(output_shader, "output.slang")) {
		output_pipeline.setup(output_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, {}, RD::PipelineColorBlendState::create_disabled());
	}
	if (uniform_buffer.is_null()) {
		uniform_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(OutputUniforms));
		if (uniform_buffer.is_valid()) {
			RD::get_singleton()->set_resource_name(uniform_buffer, "Yutrel Output UBO");
		}
	}
	if (black_fallback.is_null()) {
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
		Vector<uint8_t> black = { 0, 0, 0, 255 };
		black_fallback = RD::get_singleton()->texture_create(format, {}, { black });
	}
	return output_shader.get_shader().is_valid() && uniform_buffer.is_valid() && black_fallback.is_valid();
}

void OutputPass::render(const YutrelPipelineTargets &p_targets, const YutrelEnvironmentSettings &env, RID p_hdr,
		LutCache *p_full_post, RID p_exposure, RID p_bloom) {
	using Textures = RendererRD::TextureStorage;
	using Uniform = RD::Uniform;
	RD *rd = RD::get_singleton();
	auto *textures = Textures::get_singleton();
	auto *sets = UniformSetCacheRD::get_singleton();
	RID strip;
	uint32_t strip_size = 0;
	if (p_full_post && env.grading_lut_rid.is_valid() && env.grading_lut_intensity > 0) {
		strip = textures->texture_get_rd_texture(env.grading_lut_rid, true);
		if (strip.is_valid() && rd->texture_is_valid(strip)) {
			const RD::TextureFormat format = rd->texture_get_format(strip);
			if (format.texture_type == RD::TEXTURE_TYPE_2D && format.height >= 2 && format.height <= 64 && format.width == format.height * format.height && (format.usage_bits & RD::TEXTURE_USAGE_SAMPLING_BIT)) {
				strip_size = format.height;
			}
		}
	}
	const Color white(1, 1, 1);
	const bool grade = p_full_post && (env.grading_temperature != 0 || env.grading_tint != 0 || env.grading_contrast != 1 || env.grading_saturation != 1 || env.grading_shadows != white || env.grading_midtones != white || env.grading_highlights != white);
	const bool use_lut = p_full_post && env.tone_mapping != ToneMappingMode::NONE;
	const Rect2i region = p_targets.output_region;
	const OutputUniforms params = {
		{ env.grading_temperature, env.grading_tint, env.grading_contrast, env.grading_saturation,
				env.grading_shadows, env.grading_midtones, env.grading_highlights, uint32_t(env.tone_mapping), float(strip_size), env.grading_lut_intensity, grade },
		p_targets.output_linear, p_exposure.is_valid(), use_lut, env.debanding_enabled,
		env.exposure, p_bloom.is_valid() ? env.bloom_intensity : 0.0f, p_full_post ? env.vignette_intensity : 0.0f, env.vignette_smoothness,
		{ float(region.position.x), float(region.position.y) }, { 1.0f / region.size.x, 1.0f / region.size.y }
	};
	const YutrelScopedLabel pass_label("Yutrel OutputPass", Color(0.7, 0.9, 0.2));
	ERR_FAIL_COND(rd->buffer_update(uniform_buffer, 0, sizeof(params), &params) != OK);
	const Uniform ubo(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, uniform_buffer);
	const Uniform external(RD::UNIFORM_TYPE_TEXTURE, 1, strip_size ? strip : black_fallback);
	const Uniform sampler(RD::UNIFORM_TYPE_SAMPLER, 2, RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
	RID lut = textures->texture_rd_get_default(Textures::DEFAULT_RD_TEXTURE_3D_BLACK);
	if (use_lut) {
		ERR_FAIL_COND(!bake.prepare("color_grading_lut.slang"));
		uint64_t hash = hash_murmur3_buffer(&params.grade, sizeof(GradingData));
		hash = hash_djb2_one_64(strip.get_id(), hash);
		hash = hash_djb2_one_64(env.grading_lut_revision, hash);
		hash = hash_djb2_one_64(bake.shader.get_version(), hash);
		hash = hash_djb2_one_64(output_shader.get_version(), hash);
		if (p_full_post->texture.is_null() || p_full_post->hash != hash) {
			if (p_full_post->texture.is_null()) {
				RD::TextureFormat format;
				format.texture_type = RD::TEXTURE_TYPE_3D;
				format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
				format.width = format.height = format.depth = LUT_SIZE;
				format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
				p_full_post->texture = rd->texture_create(format, {});
				ERR_FAIL_COND(p_full_post->texture.is_null());
				rd->set_resource_name(p_full_post->texture, "Yutrel Color Grading LUT");
			}
			const RID set = sets->get_cache(bake.shader.get_shader(), 0, ubo, external, sampler, Uniform(RD::UNIFORM_TYPE_IMAGE, 3, p_full_post->texture));
			ERR_FAIL_COND(set.is_null());
			RENDER_TIMESTAMP("Yutrel Color Grading LUT");
			const YutrelScopedLabel bake_label("Yutrel Color Grading LUT", Color(0.75, 0.55, 0.9));
			const RD::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, bake.pipeline);
			rd->compute_list_bind_uniform_set(list, set, 0);
			rd->compute_list_dispatch_threads(list, LUT_SIZE, LUT_SIZE, LUT_SIZE);
			rd->compute_list_end();
			p_full_post->hash = hash;
			print_verbose("Yutrel: baked color grading LUT.");
		}
		lut = p_full_post->texture;
	}
	RENDER_TIMESTAMP("Yutrel Output");
	const RID set = sets->get_cache(output_shader.get_shader(), 0, ubo, external, sampler, Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, lut),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 4, p_hdr), Uniform(RD::UNIFORM_TYPE_TEXTURE, 5, p_bloom.is_valid() ? p_bloom : black_fallback),
			Uniform(RD::UNIFORM_TYPE_TEXTURE, 6, p_exposure.is_valid() ? p_exposure : black_fallback));
	draw_fullscreen({}, Color(0.7, 0.9, 0.2), region, p_targets.output_framebuffer, output_pipeline, set);
}

} // namespace RendererSceneRenderImplementation
