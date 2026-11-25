/**************************************************************************/
/*  rt_geometry_pass.h                                                    */
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

#include "servers/rendering/renderer_rd/yutrel/yutrel_render_data.h"

namespace RendererSceneRenderImplementation {

class RTGeometryPass {
	YutrelComputeShader shader;
	RID dummy_velocity;

public:
	struct Tier {
		RID surface[2], velocity, prev_frame_buffer;
		YutrelFrameData prev_frame;
		Size2i full_size, work_size;
		uint32_t slot = 0;
		uint64_t shader_version = 0;
		bool has_history = false;
		bool prepared = false;

		bool ensure_size(Size2i p_full_size, bool p_half_res);
		void finish_frame(const YutrelFrameData &p_frame);
		void clear();
	};

	struct History {
		Tier half, full;
		void finish_frame(const YutrelFrameData &p_frame);
		void invalidate();
		void clear();
		~History() { clear(); }
	};

	struct View {
		RID curr_surface, prev_surface, velocity, prev_frame_buffer;
		const YutrelFrameData *prev_frame = nullptr;
		Size2i work_size;
		uint32_t pixel_scale = 0;
		uint64_t shader_version = 0;
		bool has_history = false;
	};

	~RTGeometryPass();
	View prepare(History &r_history, bool p_half_res, const YutrelPipelineTargets &p_targets, RID p_frame_buffer, const YutrelFrameData &p_frame);
};

} // namespace RendererSceneRenderImplementation
