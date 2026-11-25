/**************************************************************************/
/*  rt_geometry_pass.cpp                                                  */
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

#include "rt_geometry_pass.h"

#include "screen_space_compute.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

using namespace ScreenSpaceCompute;

void RTGeometryPass::Tier::clear() {
	for (RID *rid : { &surface[0], &surface[1], &velocity, &prev_frame_buffer }) {
		if (rid->is_valid()) {
			RD::get_singleton()->free_rid(*rid);
			*rid = RID();
		}
	}
	full_size = Size2i();
	work_size = Size2i();
	slot = 0;
	has_history = false;
	prepared = false;
}

bool RTGeometryPass::Tier::ensure_size(Size2i p_full_size, bool p_half_res) {
	if (full_size == p_full_size && surface[0].is_valid()) {
		return true;
	}
	clear();
	work_size = p_half_res ? (p_full_size + Size2i(1, 1)) / 2 : p_full_size;
	for (RID &image : surface) {
		image = create_image(work_size, RD::DATA_FORMAT_R32G32_UINT, "Yutrel RT Geometry Surface");
	}
	if (p_half_res) {
		velocity = create_image(work_size, RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel Half-Res Velocity");
	}
	prev_frame_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(YutrelFrameData));
	if (prev_frame_buffer.is_valid()) {
		RD::get_singleton()->set_resource_name(prev_frame_buffer, "Yutrel RT Geometry PrevFrame UBO");
	}
	if (surface[0].is_null() || surface[1].is_null() || (p_half_res && velocity.is_null()) || prev_frame_buffer.is_null()) {
		clear();
		return false;
	}
	full_size = p_full_size;
	return true;
}

void RTGeometryPass::Tier::finish_frame(const YutrelFrameData &p_frame) {
	has_history = prepared;
	if (prepared) {
		prev_frame = p_frame;
		slot = 1 - slot;
		prepared = false;
	}
}

void RTGeometryPass::History::finish_frame(const YutrelFrameData &p_frame) {
	half.finish_frame(p_frame);
	if (!full.prepared && full.surface[0].is_valid()) {
		full.clear();
	} else {
		full.finish_frame(p_frame);
	}
}

void RTGeometryPass::History::invalidate() {
	half.has_history = false;
	full.has_history = false;
}

void RTGeometryPass::History::clear() {
	half.clear();
	full.clear();
}

RTGeometryPass::~RTGeometryPass() {
	if (dummy_velocity.is_valid()) {
		RD::get_singleton()->free_rid(dummy_velocity);
	}
}

RTGeometryPass::View RTGeometryPass::prepare(History &r_history, bool p_half_res, const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame) {
	Tier &tier = p_half_res ? r_history.half : r_history.full;
	const Size2i full_size = p_targets.region.size;
	const uint32_t pixel_scale = p_half_res ? 2 : 1;
	if (!tier.prepared || tier.full_size != full_size) {
		ERR_FAIL_COND_V(!shader.prepare("rt_geometry_prepare.slang"), View());
		const Color color(0.45, 0.6, 0.7);
		const YutrelScopedLabel label(p_half_res ? Span<char>("Yutrel Half-Res Depth/Normal") : Span<char>("Yutrel Full-Res Depth/Normal"), color);
		RENDER_TIMESTAMP(p_half_res ? "Yutrel Half-Res Depth/Normal" : "Yutrel Full-Res Depth/Normal");
		ERR_FAIL_COND_V(!tier.ensure_size(full_size, p_half_res), View());
		if (!p_half_res && dummy_velocity.is_null()) {
			dummy_velocity = create_image(Size2i(1, 1), RD::DATA_FORMAT_R16G16_SFLOAT, "Yutrel RT Geometry Unused Velocity");
			ERR_FAIL_COND_V(dummy_velocity.is_null(), View());
		}

		using Uniform = RD::Uniform;
		const RID set = UniformSetCacheRD::get_singleton()->get_cache(shader.shader.get_shader(), 0,
				Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, p_frame_buffer),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, p_targets.gbuffer_b), Uniform(RD::UNIFORM_TYPE_TEXTURE, 2, p_targets.scene_depth),
				Uniform(RD::UNIFORM_TYPE_TEXTURE, 3, p_targets.velocity), Uniform(RD::UNIFORM_TYPE_IMAGE, 4, tier.surface[tier.slot]),
				Uniform(RD::UNIFORM_TYPE_IMAGE, 5, p_half_res ? tier.velocity : dummy_velocity));
		ERR_FAIL_COND_V(set.is_null(), View());
		const uint64_t version = shader.shader.get_version();
		const bool prev_history_valid = tier.has_history && tier.shader_version == version;
		ERR_FAIL_COND_V(RD::get_singleton()->buffer_update(tier.prev_frame_buffer, 0, sizeof(p_frame), prev_history_valid ? &tier.prev_frame : &p_frame) != OK, View());
		struct PrepareParams {
			Size2i full_size;
			uint32_t pixel_scale, write_velocity;
		};
		static_assert(sizeof(PrepareParams) == 16);
		dispatch("RT Geometry Prepare", shader.pipeline, set, PrepareParams{ full_size, pixel_scale, p_half_res }, tier.work_size, RID(), color);
		tier.has_history = prev_history_valid;
		tier.shader_version = version;
		tier.prepared = true;
	}
	return { tier.surface[tier.slot], tier.surface[1 - tier.slot], p_half_res ? tier.velocity : p_targets.velocity, tier.prev_frame_buffer,
		tier.has_history ? &tier.prev_frame : &p_frame, tier.work_size, pixel_scale, tier.shader_version, tier.has_history };
}

} // namespace RendererSceneRenderImplementation
