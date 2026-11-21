/**************************************************************************/
/*  slang_compiler.h                                                      */
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
/* "Software"), to deal in the Software without restriction, including  */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

namespace slang {
struct IGlobalSession;
}

class SlangCompilerService {
	static slang::IGlobalSession *global_session;

public:
	enum class ShaderStage {
		VERTEX,
		TESSELLATION_CONTROL,
		TESSELLATION_EVALUATION,
		GEOMETRY,
		FRAGMENT,
		COMPUTE,
		RAY_GENERATION,
		INTERSECTION,
		ANY_HIT,
		CLOSEST_HIT,
		MISS,
		CALLABLE,
		MESH,
		AMPLIFICATION,
	};

	struct SlangCompileRequest {
		String module_path;
		Vector<String> search_paths;
		// When provided, the compiler loads sources exclusively from this map of relative paths.
		const HashMap<String, String> *virtual_files = nullptr;
#if defined(DEV_ENABLED) || defined(DEBUG_ENABLED)
		bool debug_info = true;
#else
		bool debug_info = false;
#endif
	};

	struct SlangEntryPoint {
		String name;
		ShaderStage stage;
		PackedByteArray spirv;
	};

	struct SlangCompileResult {
		Vector<SlangEntryPoint> entry_points;
		Vector<String> dependency_paths;
		String diagnostics;
	};

	static Error initialize();
	static void finalize();
	static Error compile_slang_to_spirv(const SlangCompileRequest &p_request, SlangCompileResult &r_result);

	static slang::IGlobalSession *get_global_session() { return global_session; }
};
