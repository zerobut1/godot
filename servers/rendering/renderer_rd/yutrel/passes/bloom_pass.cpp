/**************************************************************************/
/*  bloom_pass.cpp                                                        */
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

#include "bloom_pass.h"

#include "screen_space_compute.h"

#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

void BloomPass::History::clear() {
	if (count == 0) {
		return;
	}
	for (RID &rid : down_mips) {
		if (rid.is_valid()) {
			RD::get_singleton()->free_rid(rid);
			rid = RID();
		}
	}
	for (RID &rid : up_mips) {
		if (rid.is_valid()) {
			RD::get_singleton()->free_rid(rid);
			rid = RID();
		}
	}
	display_size = Size2i();
	count = 0;
}

bool BloomPass::History::allocate(Size2i p_size) {
	if (display_size == p_size) {
		return true;
	}
	clear();
	Size2i size = p_size;
	do {
		size = (size / 2).max(Size2i(1, 1));
		sizes[count] = size;
		down_mips[count] = ScreenSpaceCompute::create_image(size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel Bloom Down");
		ERR_FAIL_COND_V(down_mips[count].is_null(), false);
		++count;
	} while (count < MAX_MIPS && MIN(size.x, size.y) > 2);
	for (int i = 0; i < count - 1; ++i) {
		up_mips[i] = ScreenSpaceCompute::create_image(sizes[i], RD::DATA_FORMAT_R16G16B16A16_SFLOAT, "Yutrel Bloom Up");
		ERR_FAIL_COND_V(up_mips[i].is_null(), false);
	}
	display_size = p_size;
	return true;
}

RID BloomPass::render(History &h, RID p_hdr, RID p_exposure, Rect2i p_region, const YutrelEnvironmentSettings &p_environment) {
	ERR_FAIL_COND_V(!prefilter.prepare("bloom_prefilter.slang") || !downsample.prepare("bloom_downsample.slang") || !upsample.prepare("bloom_upsample.slang"), RID());
	ERR_FAIL_COND_V(!h.allocate(p_region.size), RID());
	RENDER_TIMESTAMP("Yutrel Bloom");
	using Uniform = RD::Uniform;
	auto *cache = UniformSetCacheRD::get_singleton();
	const Uniform sampler(RD::UNIFORM_TYPE_SAMPLER, 2, RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED));
	const RID fallback = RendererRD::TextureStorage::get_singleton()->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_WHITE);
	struct PrefilterParams {
		Rect2i region;
		float exposure, threshold, soft_knee;
		uint32_t use_auto_exposure;
	};
	static_assert(sizeof(PrefilterParams) == 32);
	const PrefilterParams params = { p_region, p_environment.exposure, p_environment.bloom_threshold, p_environment.bloom_soft_knee, p_exposure.is_valid() };
	const RID first_set = cache->get_cache(prefilter.shader.get_shader(), 0, Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, p_hdr),
			Uniform(RD::UNIFORM_TYPE_IMAGE, 1, h.down_mips[0]), sampler, Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_exposure.is_valid() ? p_exposure : fallback));
	ERR_FAIL_COND_V(first_set.is_null(), RID());
	const Color label_color(1.0, 0.65, 0.35);
	const YutrelScopedLabel pass_label("Yutrel Bloom", label_color);
	ScreenSpaceCompute::dispatch("Bloom Prefilter", prefilter.pipeline, first_set, params, h.sizes[0], RID(), label_color);

	static constexpr Span<char> down_labels[] = {
		"Bloom Downsample 0", "Bloom Downsample 1", "Bloom Downsample 2",
		"Bloom Downsample 3", "Bloom Downsample 4", "Bloom Downsample 5"
	};
	static constexpr Span<char> up_labels[] = {
		"Bloom Upsample 0", "Bloom Upsample 1", "Bloom Upsample 2",
		"Bloom Upsample 3", "Bloom Upsample 4"
	};
	static_assert(sizeof(down_labels) / sizeof(down_labels[0]) == MAX_MIPS);
	static_assert(sizeof(up_labels) / sizeof(up_labels[0]) == MAX_MIPS - 1);

	for (int i = 1; i < h.count; ++i) {
		const RID set = cache->get_cache(downsample.shader.get_shader(), 0, Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, h.down_mips[i - 1]),
				Uniform(RD::UNIFORM_TYPE_IMAGE, 1, h.down_mips[i]), sampler);
		ERR_FAIL_COND_V(set.is_null(), RID());
		ScreenSpaceCompute::dispatch(down_labels[i], downsample.pipeline, set, h.sizes[i], h.sizes[i], RID(), label_color);
	}
	RID coarser = h.down_mips[h.count - 1];
	for (int i = h.count - 2; i >= 0; --i) {
		const RID set = cache->get_cache(upsample.shader.get_shader(), 0, Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, coarser),
				Uniform(RD::UNIFORM_TYPE_IMAGE, 1, h.up_mips[i]), sampler, Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, h.down_mips[i]));
		ERR_FAIL_COND_V(set.is_null(), RID());
		ScreenSpaceCompute::dispatch(up_labels[i], upsample.pipeline, set, p_environment.bloom_scatter, h.sizes[i], RID(), label_color);
		coarser = h.up_mips[i];
	}
	return coarser;
}

} // namespace RendererSceneRenderImplementation
