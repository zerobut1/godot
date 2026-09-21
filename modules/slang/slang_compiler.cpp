/**************************************************************************/
/*  slang_compiler.cpp                                                    */
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

#include "slang_compiler.h"

#include "core/error/error_macros.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstring>

namespace {

SlangStage to_slang_stage(SlangCompilerService::ShaderStage p_stage) {
	switch (p_stage) {
		case SlangCompilerService::ShaderStage::VERTEX:
			return SLANG_STAGE_VERTEX;
		case SlangCompilerService::ShaderStage::TESSELLATION_CONTROL:
			return SLANG_STAGE_HULL;
		case SlangCompilerService::ShaderStage::TESSELLATION_EVALUATION:
			return SLANG_STAGE_DOMAIN;
		case SlangCompilerService::ShaderStage::GEOMETRY:
			return SLANG_STAGE_GEOMETRY;
		case SlangCompilerService::ShaderStage::FRAGMENT:
			return SLANG_STAGE_FRAGMENT;
		case SlangCompilerService::ShaderStage::COMPUTE:
			return SLANG_STAGE_COMPUTE;
		case SlangCompilerService::ShaderStage::RAY_GENERATION:
			return SLANG_STAGE_RAY_GENERATION;
		case SlangCompilerService::ShaderStage::INTERSECTION:
			return SLANG_STAGE_INTERSECTION;
		case SlangCompilerService::ShaderStage::ANY_HIT:
			return SLANG_STAGE_ANY_HIT;
		case SlangCompilerService::ShaderStage::CLOSEST_HIT:
			return SLANG_STAGE_CLOSEST_HIT;
		case SlangCompilerService::ShaderStage::MISS:
			return SLANG_STAGE_MISS;
		case SlangCompilerService::ShaderStage::CALLABLE:
			return SLANG_STAGE_CALLABLE;
		case SlangCompilerService::ShaderStage::MESH:
			return SLANG_STAGE_MESH;
		case SlangCompilerService::ShaderStage::AMPLIFICATION:
			return SLANG_STAGE_AMPLIFICATION;
	}

	return SLANG_STAGE_NONE;
}

void append_diagnostics(slang::IBlob *p_diagnostics, String &r_diagnostics) {
	if (p_diagnostics == nullptr || p_diagnostics->getBufferPointer() == nullptr || p_diagnostics->getBufferSize() == 0) {
		return;
	}

	const char *text = static_cast<const char *>(p_diagnostics->getBufferPointer());
	int64_t length = static_cast<int64_t>(p_diagnostics->getBufferSize());
	if (length > 0 && text[length - 1] == '\0') {
		--length;
	}
	if (length == 0) {
		return;
	}

	if (!r_diagnostics.is_empty() && !r_diagnostics.ends_with("\n")) {
		r_diagnostics += "\n";
	}
	r_diagnostics += String::utf8(text, length);
}

} // namespace

slang::IGlobalSession *SlangCompilerService::global_session = nullptr;

Error SlangCompilerService::initialize() {
	if (global_session != nullptr) {
		return OK;
	}

	const SlangResult result = slang_createGlobalSession(SLANG_API_VERSION, &global_session);
	if (SLANG_FAILED(result)) {
		global_session = nullptr;
		ERR_PRINT(vformat("Failed to create the Slang global session (result %d).", result));
		return ERR_CANT_CREATE;
	}

	const char *build_tag = global_session->getBuildTagString();
	print_verbose(vformat("Slang compiler initialized: %s", build_tag != nullptr ? build_tag : "unknown"));
	return OK;
}

void SlangCompilerService::finalize() {
	if (global_session == nullptr) {
		return;
	}

	global_session->release();
	global_session = nullptr;
	slang_shutdown();
}

Error SlangCompilerService::compile_hlsl_to_spirv(const String &p_source, const String &p_source_path, const String &p_entry_point, ShaderStage p_stage, PackedByteArray &r_spirv, String &r_diagnostics) {
	r_spirv.clear();
	r_diagnostics.clear();

	if (global_session == nullptr) {
		r_diagnostics = "The Slang global session is not initialized.";
		return ERR_UNCONFIGURED;
	}
	if (p_source.is_empty()) {
		r_diagnostics = "The HLSL source is empty.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_entry_point.is_empty()) {
		r_diagnostics = "The shader entry point is empty.";
		return ERR_INVALID_PARAMETER;
	}

	const SlangStage slang_stage = to_slang_stage(p_stage);
	if (slang_stage == SLANG_STAGE_NONE) {
		r_diagnostics = "The shader stage is invalid.";
		return ERR_INVALID_PARAMETER;
	}

	slang::TargetDesc target_desc = {};
	target_desc.format = SLANG_SPIRV;
	target_desc.profile = global_session->findProfile("spirv_1_5");
	target_desc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
	if (target_desc.profile == SLANG_PROFILE_UNKNOWN) {
		r_diagnostics = "Slang does not provide the spirv_1_5 target profile.";
		return ERR_UNAVAILABLE;
	}

	slang::SessionDesc session_desc = {};
	session_desc.targetCount = 1;
	session_desc.targets = &target_desc;

	Slang::ComPtr<slang::ISession> session;
	SlangResult result = global_session->createSession(session_desc, session.writeRef());
	if (SLANG_FAILED(result) || session == nullptr) {
		r_diagnostics = vformat("Failed to create the Slang compilation session (result %d).", result);
		return ERR_CANT_CREATE;
	}

	const CharString source_utf8 = p_source.utf8();
	const CharString source_path_utf8 = p_source_path.is_empty() ? CharString("runtime_shader.hlsl") : p_source_path.utf8();
	const CharString entry_point_utf8 = p_entry_point.utf8();

	Slang::ComPtr<slang::IBlob> diagnostics;
	Slang::ComPtr<slang::IModule> module(session->loadModuleFromSourceString(
			"runtime_shader",
			source_path_utf8.get_data(),
			source_utf8.get_data(),
			diagnostics.writeRef()));
	append_diagnostics(diagnostics, r_diagnostics);
	if (module == nullptr) {
		return ERR_COMPILATION_FAILED;
	}

	diagnostics.setNull();
	Slang::ComPtr<slang::IEntryPoint> entry_point;
	result = module->findAndCheckEntryPoint(entry_point_utf8.get_data(), slang_stage, entry_point.writeRef(), diagnostics.writeRef());
	append_diagnostics(diagnostics, r_diagnostics);
	if (SLANG_FAILED(result) || entry_point == nullptr) {
		return ERR_COMPILATION_FAILED;
	}

	slang::IComponentType *components[] = { module.get(), entry_point.get() };
	diagnostics.setNull();
	Slang::ComPtr<slang::IComponentType> composite_program;
	result = session->createCompositeComponentType(components, 2, composite_program.writeRef(), diagnostics.writeRef());
	append_diagnostics(diagnostics, r_diagnostics);
	if (SLANG_FAILED(result) || composite_program == nullptr) {
		return ERR_COMPILATION_FAILED;
	}

	diagnostics.setNull();
	Slang::ComPtr<slang::IComponentType> linked_program;
	result = composite_program->link(linked_program.writeRef(), diagnostics.writeRef());
	append_diagnostics(diagnostics, r_diagnostics);
	if (SLANG_FAILED(result) || linked_program == nullptr) {
		return ERR_LINK_FAILED;
	}

	diagnostics.setNull();
	Slang::ComPtr<slang::IBlob> code;
	result = linked_program->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef());
	append_diagnostics(diagnostics, r_diagnostics);
	if (SLANG_FAILED(result) || code == nullptr || code->getBufferPointer() == nullptr || code->getBufferSize() == 0) {
		return ERR_COMPILATION_FAILED;
	}

	const size_t code_size = code->getBufferSize();
	const Error resize_error = r_spirv.resize(code_size);
	if (resize_error != OK) {
		r_diagnostics = "Failed to allocate memory for the compiled SPIR-V.";
		return resize_error;
	}
	memcpy(r_spirv.ptrw(), code->getBufferPointer(), code_size);
	return OK;
}
