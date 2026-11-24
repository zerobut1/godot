/**************************************************************************/
/*  upscaler_pass.h                                                       */
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

#include "../yutrel_environment_settings.h"

#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/math/vector2i.h"

namespace RendererRD {
class FSR2Effect;
class FSR2Context;
}

namespace RendererSceneRenderImplementation {

struct YutrelPipelineTargets;

// All backends receive linear HDR, reverse-Z depth and backwards unjittered UV motion.
class UpscalerPass {
	RendererRD::FSR2Effect *fsr2 = nullptr;

public:
	struct History {
	private:
		friend class UpscalerPass;
		RendererRD::FSR2Context *fsr2_context = nullptr;
		Size2i render_size, display_size;
		RID exposure_texture;
		float exposure = 0;

	public:
		void clear();
		~History() { clear(); }
	};

	struct Camera {
		Projection projection, prev_projection;
		Transform3D transform, prev_transform;
		Vector2 jitter_px;
		float z_near, z_far;
		double time_step;
		bool reset;
	};

	~UpscalerPass();
	static float get_render_scale(UpscalerQuality p_quality);
	static uint32_t get_phase_count(Size2i p_render_size, Size2i p_display_size);
	static Vector2 get_jitter(uint32_t p_index, uint32_t p_phases);
	bool render(History &r_history, const YutrelPipelineTargets &p_targets, const Camera &p_camera,
			const YutrelEnvironmentSettings &p_environment, bool p_has_transparency);
};

} // namespace RendererSceneRenderImplementation
