/**************************************************************************/
/*  yutrel_shader.cpp                                                     */
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

#include "yutrel_shader.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "servers/rendering/rendering_device.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_SLANG_ENABLED
#include "servers/rendering/renderer_rd/shaders/yutrel/yutrel_embedded_shaders.gen.h"

#include "modules/slang/slang_compiler.h"
#endif

namespace RendererSceneRenderImplementation {

#ifdef MODULE_SLANG_ENABLED
static RD::ShaderStage to_rd_stage(SlangCompilerService::ShaderStage p_stage) {
	using Stage = SlangCompilerService::ShaderStage;
	switch (p_stage) {
		case Stage::VERTEX:
			return RD::SHADER_STAGE_VERTEX;
		case Stage::FRAGMENT:
			return RD::SHADER_STAGE_FRAGMENT;
		case Stage::TESSELLATION_CONTROL:
			return RD::SHADER_STAGE_TESSELATION_CONTROL;
		case Stage::TESSELLATION_EVALUATION:
			return RD::SHADER_STAGE_TESSELATION_EVALUATION;
		case Stage::COMPUTE:
			return RD::SHADER_STAGE_COMPUTE;
		case Stage::RAY_GENERATION:
			return RD::SHADER_STAGE_RAYGEN;
		case Stage::INTERSECTION:
			return RD::SHADER_STAGE_INTERSECTION;
		case Stage::ANY_HIT:
			return RD::SHADER_STAGE_ANY_HIT;
		case Stage::CLOSEST_HIT:
			return RD::SHADER_STAGE_CLOSEST_HIT;
		case Stage::MISS:
			return RD::SHADER_STAGE_MISS;
		default:
			return RD::SHADER_STAGE_MAX;
	}
}
#endif

bool YutrelShader::_compile(bool p_from_disk) {
#ifdef MODULE_SLANG_ENABLED
	SlangCompilerService::SlangCompileRequest request;
	// The Vulkan shader container preserves SPIR-V debug info when this engine option is enabled.
	request.debug_info = Engine::get_singleton()->is_generate_spirv_debug_info_enabled();
	HashMap<String, String> embedded_files;
	if (p_from_disk) {
		request.module_path = disk_path;
#ifdef YUTREL_SHADER_SOURCE_DIR
		request.search_paths.push_back(String::utf8(YUTREL_SHADER_SOURCE_DIR));
#endif
	} else {
		for (const YutrelEmbeddedShaders::File &file : YutrelEmbeddedShaders::files) {
			embedded_files.insert(String::utf8(file.path), String::utf8(reinterpret_cast<const char *>(file.source), file.size));
		}
		request.module_path = module_path;
		request.search_paths.push_back(".");
		request.virtual_files = &embedded_files;
	}

	SlangCompilerService::SlangCompileResult result;
	const Error error = SlangCompilerService::compile_slang_to_spirv(request, result);
	ERR_FAIL_COND_V_MSG(error != OK, false, vformat("Yutrel: failed to compile %s (error %d).\n%s", request.module_path, error, result.diagnostics));

	Vector<RD::ShaderStageSPIRVData> stages;
	for (const SlangCompilerService::SlangEntryPoint &entry_point : result.entry_points) {
		const RD::ShaderStage stage = to_rd_stage(entry_point.stage);
		ERR_FAIL_COND_V_MSG(stage == RD::SHADER_STAGE_MAX, false, vformat("Yutrel: %s has an unsupported shader stage for '%s'.", module_path, entry_point.name));
		stages.push_back({ stage, entry_point.spirv });
	}

	RD *rd = RD::get_singleton();
	const RID new_shader = rd->shader_create_from_spirv(stages, module_path);
	ERR_FAIL_COND_V_MSG(new_shader.is_null(), false, vformat("Yutrel: failed to create the shader for %s.", request.module_path));
	rd->set_resource_name(new_shader, module_path);

	if (p_from_disk) {
		HashMap<String, uint64_t> next_mtimes;
		next_mtimes.insert(disk_path, observed_mtimes[disk_path]);
		for (const String &path : result.dependency_paths) {
			const uint64_t *observed = observed_mtimes.getptr(path);
			next_mtimes.insert(path, observed ? *observed : FileAccess::get_modified_time(path));
		}
		observed_mtimes = std::move(next_mtimes);
	}
	if (shader.is_valid()) {
		// RenderingDevice also frees pipelines that depend on the old shader.
		rd->free_rid(shader);
	}
	shader = new_shader;
	++version;
	return true;
#else
	ERR_FAIL_V_MSG(false, "Yutrel requires the Slang module to compile shaders.");
#endif
}

void YutrelShader::load(const String &p_module_path) {
	ERR_FAIL_COND(is_loaded() || p_module_path.is_empty());
	module_path = p_module_path;
	bool from_disk = false;
#ifdef YUTREL_SHADER_SOURCE_DIR
	disk_path = String::utf8(YUTREL_SHADER_SOURCE_DIR).path_join(module_path).simplify_path();
	observed_mtimes.insert(disk_path, FileAccess::get_modified_time(disk_path));
	from_disk = FileAccess::exists(disk_path);
#endif
	if (_compile(from_disk)) {
		print_verbose(vformat("Yutrel: loaded %s from %s.", module_path, from_disk ? disk_path : String("embedded sources")));
	}
}

void YutrelShader::poll_hot_reload() {
	if (observed_mtimes.is_empty()) {
		return;
	}
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (now - last_poll_msec < 300) {
		return;
	}
	last_poll_msec = now;

	bool changed = false;
	for (KeyValue<String, uint64_t> &file : observed_mtimes) {
		const uint64_t mtime = FileAccess::get_modified_time(file.key);
		if (file.value != mtime) {
			file.value = mtime;
			changed = true;
		}
	}
	if (changed && _compile(true)) {
		print_line(vformat("Yutrel: hot-reloaded %s.", module_path));
	}
}

YutrelShader::~YutrelShader() {
	if (shader.is_valid()) {
		RD::get_singleton()->free_rid(shader);
	}
}

} // namespace RendererSceneRenderImplementation
