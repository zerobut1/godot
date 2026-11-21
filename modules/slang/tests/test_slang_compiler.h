/**************************************************************************/
/*  test_slang_compiler.h                                                 */
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

#include "../slang_compiler.h"

#include "tests/test_macros.h"

#include <cstring>

namespace TestSlangCompiler {

TEST_CASE("[Slang] Compile a native compute shader to SPIR-V") {
	using Compiler = SlangCompilerService;
	HashMap<String, String> files;
	files.insert("compute.slang", R"(
RWStructuredBuffer<uint> output_buffer;

[shader("compute")]
[numthreads(8, 1, 1)]
void compute_main(uint3 id : SV_DispatchThreadID) {
	output_buffer[id.x] = id.x * 2 + 1;
}
)");
	Compiler::SlangCompileRequest request;
	request.module_path = "compute.slang";
	request.virtual_files = &files;
	Compiler::SlangCompileResult result;
	const Error error = Compiler::compile_slang_to_spirv(request, result);

	INFO(result.diagnostics);
	REQUIRE(error == OK);
	REQUIRE(result.entry_points.size() == 1);
	const Compiler::SlangEntryPoint &entry_point = result.entry_points[0];
	CHECK(entry_point.stage == Compiler::ShaderStage::COMPUTE);
	CHECK(entry_point.name == "compute_main");
	const PackedByteArray &spirv = entry_point.spirv;
	REQUIRE(spirv.size() >= 20);
	CHECK(spirv.size() % sizeof(uint32_t) == 0);

	uint32_t magic = 0;
	memcpy(&magic, spirv.ptr(), sizeof(magic));
	CHECK(magic == 0x07230203);
}

TEST_CASE("[Slang] Compile vertex and fragment entry points from one module") {
	using Compiler = SlangCompilerService;
	HashMap<String, String> files;
	files.insert("graphics.slang", R"(
struct Varyings {
	float4 position : SV_Position;
	float3 color : COLOR0;
};

[shader("vertex")]
Varyings vertex_main(uint vertex_id : SV_VulkanVertexID) {
	return { float4(float(vertex_id), 0, 0, 1), float3(1, 0.5, 0) };
}

[shader("fragment")]
float4 fragment_main(Varyings input) : SV_Target0 {
	return float4(input.color, 1);
}
)");
	Compiler::SlangCompileRequest request;
	request.module_path = "graphics.slang";
	request.virtual_files = &files;
	Compiler::SlangCompileResult result;
	const Error error = Compiler::compile_slang_to_spirv(request, result);

	INFO(result.diagnostics);
	REQUIRE(error == OK);
	REQUIRE(result.entry_points.size() == 2);
	for (const Compiler::SlangEntryPoint &entry_point : result.entry_points) {
		REQUIRE(entry_point.spirv.size() >= 20);
		CHECK(entry_point.spirv.size() % sizeof(uint32_t) == 0);
		uint32_t magic = 0;
		memcpy(&magic, entry_point.spirv.ptr(), sizeof(magic));
		CHECK(magic == 0x07230203);
		if (entry_point.stage == Compiler::ShaderStage::VERTEX) {
			CHECK(entry_point.name == "vertex_main");
		} else {
			CHECK(entry_point.stage == Compiler::ShaderStage::FRAGMENT);
			CHECK(entry_point.name == "fragment_main");
		}
	}
	CHECK(result.entry_points[0].stage != result.entry_points[1].stage);
}

TEST_CASE("[Slang] Resolve imported modules from embedded sources") {
	using Compiler = SlangCompilerService;
	HashMap<String, String> files;
	files.insert("effects/tint.slang", R"(
public float4 get_tint() { return float4(0.1, 0.3, 0.5, 1); }
)");
	files.insert("effects/example.slang", R"(
import tint;

[shader("fragment")]
float4 fragment_main() : SV_Target0 { return get_tint(); }
)");
	Compiler::SlangCompileRequest request;
	request.module_path = "effects/example.slang";
	request.virtual_files = &files;
	Compiler::SlangCompileResult result;
	const Error error = Compiler::compile_slang_to_spirv(request, result);

	INFO(result.diagnostics);
	REQUIRE(error == OK);
	REQUIRE(result.entry_points.size() == 1);
	CHECK_FALSE(result.entry_points[0].spirv.is_empty());
	CHECK(result.dependency_paths.has("effects/example.slang"));
	CHECK(result.dependency_paths.has("effects/tint.slang"));
}

TEST_CASE("[Slang] Return diagnostics for an invalid native module") {
	using Compiler = SlangCompilerService;
	HashMap<String, String> files;
	files.insert("invalid.slang", R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0 { return undefined_tint; }
)");
	Compiler::SlangCompileRequest request;
	request.module_path = "invalid.slang";
	request.virtual_files = &files;
	Compiler::SlangCompileResult result;

	CHECK(Compiler::compile_slang_to_spirv(request, result) == ERR_COMPILATION_FAILED);
	CHECK(result.entry_points.is_empty());
	CHECK(result.diagnostics.contains("undefined_tint"));
}

} // namespace TestSlangCompiler
