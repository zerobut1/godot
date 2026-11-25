/**************************************************************************/
/*  upscaler_pass.cpp                                                     */
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

#include "upscaler_pass.h"

#include "servers/rendering/renderer_rd/effects/fsr2.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

float UpscalerPass::get_render_scale(UpscalerQuality p_quality) {
	switch (p_quality) {
		case UpscalerQuality::QUALITY: return 1.0f / 1.5f;
		case UpscalerQuality::BALANCED: return 1.0f / 1.7f;
		case UpscalerQuality::PERFORMANCE: return 0.5f;
		default: return 1.0f;
	}
}

uint32_t UpscalerPass::get_phase_count(Size2i p_render_size, Size2i p_display_size) {
	return MAX(8, ffxFsr2GetJitterPhaseCount(p_render_size.x, p_display_size.x));
}

Vector2 UpscalerPass::get_jitter(uint32_t p_index, uint32_t p_phases) {
	float x, y;
	ffxFsr2GetJitterOffset(&x, &y, p_index % p_phases, p_phases);
	return Vector2(x, y);
}

void UpscalerPass::History::clear() {
	if (fsr2_context) {
		memdelete(fsr2_context);
		fsr2_context = nullptr;
	}
	if (exposure_texture.is_valid()) {
		RD::get_singleton()->free_rid(exposure_texture);
		exposure_texture = RID();
	}
	exposure = 0;
}

UpscalerPass::~UpscalerPass() {
	if (fsr2) {
		memdelete(fsr2);
	}
}

bool UpscalerPass::render(History &h, const YutrelPipelineTargets &p_targets, const Camera &p_camera,
		const YutrelEnvironmentSettings &p_environment, bool p_has_transparency, RID p_exposure_override) {
	ERR_FAIL_COND_V(p_environment.upscaler_mode != UpscalerMode::FSR2, false);
	if (!fsr2) {
		fsr2 = memnew(RendererRD::FSR2Effect);
	}
	const Size2i size = p_targets.region.size;
	const Size2i display_size = p_targets.output_region.size;
	const bool recreate = !h.fsr2_context || size != h.render_size || display_size != h.display_size;
	if (recreate) {
		h.clear();
		// SPD binds mip 5 of a half-resolution pyramid, even for tiny viewports.
		const Size2i capacity(MAX(size.x, 64), MAX(size.y, 16));
		h.fsr2_context = fsr2->create_context(capacity, display_size);
		ERR_FAIL_NULL_V(h.fsr2_context, false);
		h.render_size = size;
		h.display_size = display_size;
	}

	const float exposure = MAX(p_environment.exposure, 1e-4f);
	if (p_exposure_override.is_null() && h.exposure != exposure) {
		Vector<uint8_t> data;
		const float values[] = { exposure, 0 };
		data.resize(sizeof(values));
		memcpy(data.ptrw(), values, sizeof(values));
		RD *rd = RD::get_singleton();
		if (h.exposure_texture.is_null()) {
			RD::TextureFormat format;
			format.format = RD::DATA_FORMAT_R32G32_SFLOAT;
			format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
			h.exposure_texture = rd->texture_create(format, {}, { data });
			ERR_FAIL_COND_V(h.exposure_texture.is_null(), false);
		} else {
			ERR_FAIL_COND_V(rd->texture_update(h.exposure_texture, 0, data) != OK, false);
		}
		h.exposure = exposure;
	}

	RendererRD::FSR2Effect::Parameters params;
	params.context = h.fsr2_context;
	params.internal_size = size;
	params.color = p_targets.scene_color;
	params.depth = p_targets.scene_depth;
	params.velocity = p_targets.velocity;
	params.reactive = p_has_transparency ? p_targets.reactive_mask : RID();
	params.exposure = p_exposure_override.is_valid() ? p_exposure_override : h.exposure_texture;
	params.output = p_targets.upscaled_color;
	params.z_near = p_camera.z_far;
	params.z_far = p_camera.z_near;
	params.fovy = CLAMP(2.0f * Math::atan(1.0f / MAX(Math::abs(float(p_camera.projection.columns[1][1])), 1e-4f)), 0.01f, float(Math::PI) - 0.01f);
	params.jitter = p_camera.jitter_px;
	params.delta_time = p_camera.time_step > 0 ? CLAMP(float(p_camera.time_step * 1000.0), 1.0f, 1000.0f) : 16.6667f;
	params.sharpness = CLAMP(p_environment.upscaler_sharpness, 0.0f, 1.0f);
	params.reset_accumulation = recreate || p_camera.reset;
	Projection correction;
	correction.set_depth_correction(true, true, false);
	params.reprojection = (correction * p_camera.prev_projection) * Projection(p_camera.prev_transform.affine_inverse() * p_camera.transform) * (correction * p_camera.projection).inverse();
	fsr2->upscale(params);
	return true;
}

} // namespace RendererSceneRenderImplementation
