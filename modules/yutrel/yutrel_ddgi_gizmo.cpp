/**************************************************************************/
/*  yutrel_ddgi_gizmo.cpp                                                 */
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

#include "yutrel_ddgi_gizmo.h"

#ifdef TOOLS_ENABLED
#include "yutrel_ddgi_volume.h"

#include "editor/scene/3d/node_3d_editor_gizmos.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"

class YutrelDDGIGizmoPlugin : public EditorNode3DGizmoPlugin {
public:
	bool has_gizmo(Node3D *p_node) override { return Object::cast_to<YutrelDDGIVolume>(p_node) != nullptr; }
	String get_gizmo_name() const override { return "YutrelDDGIVolume"; }

	YutrelDDGIGizmoPlugin() {
		create_material("bounds", Color(0.3f, 0.8f, 0.65f));
	}
	void redraw(EditorNode3DGizmo *p_gizmo) override {
		p_gizmo->clear();
		const auto *volume = Object::cast_to<YutrelDDGIVolume>(p_gizmo->get_node_3d());
		const Transform3D transform = volume->get_global_transform();
		if (transform.basis.determinant() == 0) {
			return;
		}
		const Transform3D to_local = transform.affine_inverse();
		const AABB bounds = volume->get_world_bounds();
		Vector<Vector3> lines;
		for (int i = 0; i < 12; ++i) {
			Vector3 a, b;
			bounds.get_edge(i, a, b);
			lines.push_back(to_local.xform(a));
			lines.push_back(to_local.xform(b));
		}
		p_gizmo->add_lines(lines, get_material("bounds", p_gizmo));
		p_gizmo->add_collision_segments(lines);
	}
};

YutrelDDGIEditorPlugin::YutrelDDGIEditorPlugin() {
	Node3DEditor::get_singleton()->add_gizmo_plugin(Ref<YutrelDDGIGizmoPlugin>(memnew(YutrelDDGIGizmoPlugin)));
}
#endif
