/**************************************************************************/
/*  ibl_pass.h                                                            */
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

#include "core/templates/local_vector.h"
#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_environment_settings.h"
#include "servers/rendering/renderer_rd/yutrel/yutrel_shader.h"

#include <array>

namespace RendererSceneRenderImplementation {

struct YutrelPipelineTargets;

class IBLPass {
	static constexpr uint32_t SOURCE_SIZE = 512;
	static constexpr uint32_t SOURCE_MIPS = 10;
	static constexpr uint32_t PREFILTER_SIZE = 256;
	static constexpr uint32_t PREFILTER_MIPS = 7;
	static constexpr uint32_t SH_SOURCE_MIP = 3;

	struct BakeShader {
		YutrelShader shader;
		RID pipeline;

		~BakeShader();
		bool prepare(const char *p_path);
	};

	struct BakeKey {
		SkyMode mode = SkyMode::CONSTANT_COLOR;
		Color color;
		RID panorama;
		std::array<uint64_t, 4> shader_versions = {};

		bool operator==(const BakeKey &p_other) const {
			return mode == p_other.mode && color == p_other.color && panorama == p_other.panorama && shader_versions == p_other.shader_versions;
		}
	};

	struct Cube {
		RID texture;
		LocalVector<RID> faces;

		~Cube();
		bool initialize(uint32_t p_size, uint32_t p_mips, const char *p_name);
		RID face(uint32_t p_mip, uint32_t p_face) const { return faces[p_mip * 6 + p_face]; }
	};

	YutrelShader lighting_shader;
	BakeShader equirect;
	BakeShader downsample;
	BakeShader prefilter;
	BakeShader convolve_sh;
	PipelineCacheRD lighting_pipeline;
	Cube source;
	Cube filtered;
	RID panorama_sampler;
	RID cube_sampler;
	RID sh_source_view;
	RID sh_buffer;
	RID sh_partial_sums;
	BakeKey baked_key;
	bool baked = false;

	bool _initialize_resources();
	bool _bake(const BakeKey &p_key);

public:
	~IBLPass();

	void prepare();
	bool prepare_environment(const YutrelEnvironmentSettings &p_environment);
	RID get_source_cubemap() const { return baked ? source.texture : RID(); }
	RID get_prefilter_cubemap() const { return filtered.texture; }
	RID get_sh_buffer() const { return sh_buffer; }
	static constexpr float get_prefilter_max_mip() { return PREFILTER_MIPS - 1.0f; }
	RID get_cube_sampler() const { return cube_sampler; }

	void render(const YutrelPipelineTargets &p_targets, RID p_frame_buffer, RID p_dfg_lut, const YutrelSkyParams &p_sky);
};

} // namespace RendererSceneRenderImplementation
