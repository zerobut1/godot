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

TEST_CASE("[Slang] Compile HLSL compute shader to SPIR-V") {
	const String source = R"(
[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> outputBuffer : register(u0, space0);

[numthreads(8, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	outputBuffer[id.x] = id.x * 2 + 1;
}
)";

	PackedByteArray spirv;
	String diagnostics;
	const Error error = SlangCompilerService::compile_hlsl_to_spirv(
			source,
			"test_compute.hlsl",
			"main",
			SlangCompilerService::ShaderStage::COMPUTE,
			spirv,
			diagnostics);

	INFO(diagnostics);
	REQUIRE(error == OK);
	REQUIRE(spirv.size() >= 20);
	CHECK(spirv.size() % sizeof(uint32_t) == 0);

	uint32_t magic = 0;
	memcpy(&magic, spirv.ptr(), sizeof(magic));
	CHECK(magic == 0x07230203);
}

TEST_CASE("[Slang] Return diagnostics for invalid HLSL") {
	const String source = R"(
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	this is not valid HLSL;
}
)";

	PackedByteArray spirv;
	String diagnostics;
	const Error error = SlangCompilerService::compile_hlsl_to_spirv(
			source,
			"invalid_compute.hlsl",
			"main",
			SlangCompilerService::ShaderStage::COMPUTE,
			spirv,
			diagnostics);

	CHECK(error == ERR_COMPILATION_FAILED);
	CHECK(spirv.is_empty());
	CHECK_FALSE(diagnostics.is_empty());
}

} // namespace TestSlangCompiler
