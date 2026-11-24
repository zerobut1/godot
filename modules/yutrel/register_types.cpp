/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "yutrel_ddgi_volume.h"
#include "yutrel_environment.h"
#include "yutrel_material.h"

#ifdef TOOLS_ENABLED
#include "yutrel_ddgi_gizmo.h"
#endif

#include "core/object/class_db.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GLTF_ENABLED
#include "gltf_document_extension_yutrel.h"

#include "modules/gltf/gltf_document.h"
#endif

void initialize_yutrel_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(YutrelMaterial);
		GDREGISTER_CLASS(YutrelEnvironment);
		GDREGISTER_CLASS(YutrelDDGIVolume);
#ifdef MODULE_GLTF_ENABLED
		GDREGISTER_CLASS(GLTFDocumentExtensionYutrel);
		Ref<GLTFDocumentExtensionYutrel> extension;
		extension.instantiate();
		GLTFDocument::register_gltf_document_extension(extension);
#endif
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<YutrelDDGIEditorPlugin>();
	}
#endif
}

void uninitialize_yutrel_module(ModuleInitializationLevel p_level) {
}
