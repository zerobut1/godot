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
#include "core/os/memory.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/variant.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstring>

namespace {

class EmbeddedSlangFileSystem final : public ISlangFileSystemExt {
	const HashMap<String, String> &files;
	SafeNumeric<uint32_t> ref_count{ 1 };

	static SlangResult get_path_blob(const String &p_path, ISlangBlob **r_blob) {
		const CharString path = p_path.utf8();
		*r_blob = slang_createBlob(path.get_data(), path.length() + 1);
		return *r_blob ? SLANG_OK : SLANG_E_OUT_OF_MEMORY;
	}

public:
	explicit EmbeddedSlangFileSystem(const HashMap<String, String> &p_files) :
			files(p_files) {}

	void *SLANG_MCALL castAs(const SlangUUID &p_uuid) override {
		static constexpr SlangUUID interfaces[] = {
			ISlangUnknown::getTypeGuid(),
			ISlangCastable::getTypeGuid(),
			ISlangFileSystem::getTypeGuid(),
			ISlangFileSystemExt::getTypeGuid(),
		};
		for (const SlangUUID &uuid : interfaces) {
			if (memcmp(&p_uuid, &uuid, sizeof(uuid)) == 0) {
				return static_cast<ISlangFileSystemExt *>(this);
			}
		}
		return nullptr;
	}

	SlangResult SLANG_MCALL queryInterface(const SlangUUID &p_uuid, void **r_object) override {
		*r_object = castAs(p_uuid);
		if (!*r_object) {
			return SLANG_E_NO_INTERFACE;
		}
		addRef();
		return SLANG_OK;
	}

	uint32_t SLANG_MCALL addRef() override { return ref_count.increment(); }

	uint32_t SLANG_MCALL release() override {
		const uint32_t remaining = ref_count.decrement();
		if (remaining == 0) {
			memdelete(this);
		}
		return remaining;
	}

	SlangResult SLANG_MCALL loadFile(const char *p_path, ISlangBlob **r_blob) override {
		*r_blob = nullptr;
		const String *source = files.getptr(String::utf8(p_path).simplify_path());
		if (!source) {
			return SLANG_E_NOT_FOUND;
		}
		const CharString utf8 = source->utf8();
		// Slang's blob factory requires at least one byte, even for an empty source file.
		*r_blob = slang_createBlob(utf8.get_data(), MAX(1, utf8.length()));
		return *r_blob ? SLANG_OK : SLANG_E_OUT_OF_MEMORY;
	}

	SlangResult SLANG_MCALL getFileUniqueIdentity(const char *p_path, ISlangBlob **r_blob) override {
		const String path = String::utf8(p_path).simplify_path();
		return files.has(path) ? get_path_blob(path, r_blob) : SLANG_E_NOT_FOUND;
	}

	SlangResult SLANG_MCALL calcCombinedPath(SlangPathType p_from_type, const char *p_from, const char *p_path, ISlangBlob **r_blob) override {
		const String from = String::utf8(p_from);
		const String directory = p_from_type == SLANG_PATH_TYPE_FILE ? from.get_base_dir() : from;
		return get_path_blob(directory.path_join(String::utf8(p_path)).simplify_path(), r_blob);
	}

	SlangResult SLANG_MCALL getPathType(const char *p_path, SlangPathType *r_type) override {
		if (!files.has(String::utf8(p_path).simplify_path())) {
			return SLANG_E_NOT_FOUND;
		}
		*r_type = SLANG_PATH_TYPE_FILE;
		return SLANG_OK;
	}

	SlangResult SLANG_MCALL getPath(PathKind p_kind, const char *p_path, ISlangBlob **r_blob) override {
		if (p_kind == PathKind::OperatingSystem) {
			return SLANG_E_NOT_IMPLEMENTED;
		}
		return get_path_blob(String::utf8(p_path).simplify_path(), r_blob);
	}

	void SLANG_MCALL clearCache() override {}
	SlangResult SLANG_MCALL enumeratePathContents(const char *, FileSystemContentsCallBack, void *) override { return SLANG_E_NOT_IMPLEMENTED; }
	OSPathKind SLANG_MCALL getOSPathKind() override { return OSPathKind::None; }
};

bool from_slang_stage(SlangStage p_stage, SlangCompilerService::ShaderStage &r_stage) {
	using Stage = SlangCompilerService::ShaderStage;
	switch (p_stage) {
		case SLANG_STAGE_VERTEX:
			r_stage = Stage::VERTEX;
			break;
		case SLANG_STAGE_HULL:
			r_stage = Stage::TESSELLATION_CONTROL;
			break;
		case SLANG_STAGE_DOMAIN:
			r_stage = Stage::TESSELLATION_EVALUATION;
			break;
		case SLANG_STAGE_GEOMETRY:
			r_stage = Stage::GEOMETRY;
			break;
		case SLANG_STAGE_FRAGMENT:
			r_stage = Stage::FRAGMENT;
			break;
		case SLANG_STAGE_COMPUTE:
			r_stage = Stage::COMPUTE;
			break;
		case SLANG_STAGE_RAY_GENERATION:
			r_stage = Stage::RAY_GENERATION;
			break;
		case SLANG_STAGE_INTERSECTION:
			r_stage = Stage::INTERSECTION;
			break;
		case SLANG_STAGE_ANY_HIT:
			r_stage = Stage::ANY_HIT;
			break;
		case SLANG_STAGE_CLOSEST_HIT:
			r_stage = Stage::CLOSEST_HIT;
			break;
		case SLANG_STAGE_MISS:
			r_stage = Stage::MISS;
			break;
		case SLANG_STAGE_CALLABLE:
			r_stage = Stage::CALLABLE;
			break;
		case SLANG_STAGE_MESH:
			r_stage = Stage::MESH;
			break;
		case SLANG_STAGE_AMPLIFICATION:
			r_stage = Stage::AMPLIFICATION;
			break;
		default:
			return false;
	}
	return true;
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

Error SlangCompilerService::compile_slang_to_spirv(const SlangCompileRequest &p_request, SlangCompileResult &r_result) {
	r_result = {};
	if (!global_session) {
		r_result.diagnostics = "The Slang global session is not initialized.";
		return ERR_UNCONFIGURED;
	}
	if (p_request.module_path.is_empty()) {
		r_result.diagnostics = "The Slang module path is empty.";
		return ERR_INVALID_PARAMETER;
	}

	slang::TargetDesc target_desc = {};
	target_desc.format = SLANG_SPIRV;
	target_desc.profile = global_session->findProfile("spirv_1_5");
	target_desc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
	if (target_desc.profile == SLANG_PROFILE_UNKNOWN) {
		r_result.diagnostics = "Slang does not provide the spirv_1_5 target profile.";
		return ERR_UNAVAILABLE;
	}

	LocalVector<CharString> search_paths_utf8;
	LocalVector<const char *> search_paths;
	search_paths_utf8.reserve(p_request.search_paths.size());
	for (const String &path : p_request.search_paths) {
		search_paths_utf8.push_back(path.utf8());
		search_paths.push_back(search_paths_utf8[search_paths_utf8.size() - 1].get_data());
	}

	Slang::ComPtr<ISlangFileSystem> file_system;
	if (p_request.virtual_files) {
		file_system.attach(memnew(EmbeddedSlangFileSystem(*p_request.virtual_files)));
	}

	const slang::CompilerOptionEntry debug_option = {
		slang::CompilerOptionName::DebugInformation,
		{ slang::CompilerOptionValueKind::Int, p_request.debug_info ? SLANG_DEBUG_INFO_LEVEL_STANDARD : SLANG_DEBUG_INFO_LEVEL_NONE },
	};
	slang::SessionDesc session_desc = {};
	session_desc.targets = &target_desc;
	session_desc.targetCount = 1;
	session_desc.searchPaths = search_paths.ptr();
	session_desc.searchPathCount = search_paths.size();
	session_desc.fileSystem = file_system.get();
	session_desc.compilerOptionEntries = &debug_option;
	session_desc.compilerOptionEntryCount = 1;

	Slang::ComPtr<slang::ISession> session;
	SlangResult result = global_session->createSession(session_desc, session.writeRef());
	if (SLANG_FAILED(result) || !session) {
		r_result.diagnostics = vformat("Failed to create the Slang compilation session (result %d).", result);
		return ERR_CANT_CREATE;
	}

	const CharString module_path = p_request.module_path.utf8();
	Slang::ComPtr<slang::IBlob> diagnostics;
	Slang::ComPtr<slang::IModule> module(session->loadModule(module_path.get_data(), diagnostics.writeRef()));
	append_diagnostics(diagnostics, r_result.diagnostics);
	if (!module) {
		return ERR_COMPILATION_FAILED;
	}

	for (SlangInt32 i = 0; i < module->getDependencyFileCount(); ++i) {
		if (const char *path = module->getDependencyFilePath(i)) {
			r_result.dependency_paths.push_back(String::utf8(path).simplify_path());
		}
	}

	const SlangInt32 entry_point_count = module->getDefinedEntryPointCount();
	if (entry_point_count == 0) {
		r_result.diagnostics += "The Slang module has no entry points marked with [shader(...)].";
		return ERR_COMPILATION_FAILED;
	}

	LocalVector<Slang::ComPtr<slang::IEntryPoint>> entry_points;
	entry_points.resize(entry_point_count);
	LocalVector<slang::IComponentType *> components;
	components.push_back(module.get());
	for (SlangInt32 i = 0; i < entry_point_count; ++i) {
		result = module->getDefinedEntryPoint(i, entry_points[i].writeRef());
		if (SLANG_FAILED(result) || !entry_points[i]) {
			r_result.diagnostics += vformat("Failed to retrieve Slang entry point %d (result %d).", i, result);
			return ERR_COMPILATION_FAILED;
		}
		components.push_back(entry_points[i].get());
	}

	// Link every entry point in the same program so they share a binding layout.
	Slang::ComPtr<slang::IComponentType> composite_program;
	result = session->createCompositeComponentType(components.ptr(), components.size(), composite_program.writeRef(), diagnostics.writeRef());
	append_diagnostics(diagnostics, r_result.diagnostics);
	if (SLANG_FAILED(result) || !composite_program) {
		return ERR_COMPILATION_FAILED;
	}

	Slang::ComPtr<slang::IComponentType> linked_program;
	result = composite_program->link(linked_program.writeRef(), diagnostics.writeRef());
	append_diagnostics(diagnostics, r_result.diagnostics);
	if (SLANG_FAILED(result) || !linked_program) {
		return ERR_LINK_FAILED;
	}

	slang::ProgramLayout *layout = linked_program->getLayout(0, diagnostics.writeRef());
	append_diagnostics(diagnostics, r_result.diagnostics);
	if (!layout) {
		return ERR_COMPILATION_FAILED;
	}

	Vector<SlangEntryPoint> compiled_entry_points;
	for (SlangInt32 i = 0; i < entry_point_count; ++i) {
		slang::EntryPointReflection *reflection = layout->getEntryPointByIndex(i);
		SlangEntryPoint entry_point;
		if (!reflection || !from_slang_stage(reflection->getStage(), entry_point.stage)) {
			r_result.diagnostics += vformat("Unsupported stage for Slang entry point %d.", i);
			return ERR_UNAVAILABLE;
		}
		entry_point.name = String::utf8(reflection->getName());

		Slang::ComPtr<slang::IBlob> code;
		result = linked_program->getEntryPointCode(i, 0, code.writeRef(), diagnostics.writeRef());
		append_diagnostics(diagnostics, r_result.diagnostics);
		if (SLANG_FAILED(result) || !code || !code->getBufferPointer() || code->getBufferSize() == 0) {
			return ERR_COMPILATION_FAILED;
		}

		const Error resize_error = entry_point.spirv.resize(code->getBufferSize());
		if (resize_error != OK) {
			r_result.diagnostics += "Failed to allocate memory for the compiled SPIR-V.";
			return resize_error;
		}
		memcpy(entry_point.spirv.ptrw(), code->getBufferPointer(), code->getBufferSize());
		compiled_entry_points.push_back(entry_point);
	}

	r_result.entry_points = std::move(compiled_entry_points);
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
