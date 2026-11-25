/**************************************************************************/
/*  auto_exposure_pass.cpp                                                */
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

#include "auto_exposure_pass.h"

#include "screen_space_compute.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

void AutoExposurePass::History::clear() {
	for (RID resource : { histogram_buffer, exposure_texture }) {
		if (resource.is_valid()) {
			RD::get_singleton()->free_rid(resource);
		}
	}
	histogram_buffer = RID();
	exposure_texture = RID();
	initialized = false;
}

bool AutoExposurePass::render(History &h, RID p_color, Rect2i p_region, const YutrelEnvironmentSettings &p_environment, float p_time_step) {
	ERR_FAIL_COND_V(!histogram.prepare("auto_exposure_histogram.slang") || !adapt.prepare("auto_exposure_adapt.slang"), false);
	RENDER_TIMESTAMP("Yutrel Auto Exposure");
	RD *rd = RD::get_singleton();
	constexpr uint32_t histogram_bytes = 256 * sizeof(uint32_t);
	if (h.histogram_buffer.is_null()) {
		h.histogram_buffer = rd->storage_buffer_create(histogram_bytes);
		ERR_FAIL_COND_V(h.histogram_buffer.is_null(), false);
	}
	if (h.exposure_texture.is_null()) {
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R32G32_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
		h.exposure_texture = rd->texture_create(format, {});
		ERR_FAIL_COND_V(h.exposure_texture.is_null(), false);
	}
	auto *cache = UniformSetCacheRD::get_singleton();
	const RID histogram_set = cache->get_cache(histogram.shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, p_color), RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, h.histogram_buffer));
	const RID adapt_set = cache->get_cache(adapt.shader.get_shader(), 0,
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, h.histogram_buffer), RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, h.exposure_texture));
	ERR_FAIL_COND_V(histogram_set.is_null() || adapt_set.is_null(), false);
	ERR_FAIL_COND_V(rd->buffer_clear(h.histogram_buffer, 0, histogram_bytes) != OK, false);
	ScreenSpaceCompute::dispatch(histogram.pipeline, histogram_set, p_region, p_region.size);
	struct AdaptParams {
		float min_ev, max_ev, compensation, speed_up, speed_down, dt;
		uint32_t reset;
	};
	const AdaptParams params = { p_environment.auto_exposure_min_ev, p_environment.auto_exposure_max_ev, p_environment.auto_exposure_compensation,
		p_environment.auto_exposure_speed_up, p_environment.auto_exposure_speed_down, p_time_step, !h.initialized };
	static_assert(sizeof(AdaptParams) == 28 && sizeof(Rect2i) == 16);
	ScreenSpaceCompute::dispatch(adapt.pipeline, adapt_set, params, Size2i(256, 1));
	h.initialized = true;
	return true;
}

} // namespace RendererSceneRenderImplementation
