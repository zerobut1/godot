/**************************************************************************/
/*  editor_gizmo_pass.cpp                                                */
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

#include "editor_gizmo_pass.h"

#ifdef TOOLS_ENABLED

#include "core/string/print_string.h"
#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace RendererSceneRenderImplementation {

namespace {

using MaterialStorage = RendererRD::MaterialStorage;
using MeshStorage = RendererRD::MeshStorage;
using TextureStorage = RendererRD::TextureStorage;

enum GizmoFlags : uint32_t {
	VERTEX_COLOR = 1 << 0,
	SRGB_VERTEX_COLOR = 1 << 1,
	BILLBOARD = 1 << 2,
	FIXED_SIZE = 1 << 3,
	GRID = 1 << 4,
	AXIS_RING = 1 << 5,
	VIEW_RING = 1 << 6,
	ORIGIN_AXIS = 1 << 7,
	POINT_SPRITE = 1 << 8,
};

// MaterialStorage exposes parameter values but keeps each material's shader and
// priority private. These adapters capture the editor metadata through its
// normal spatial material hooks. All actual rendering goes through Slang.
struct GizmoShaderData : MaterialStorage::ShaderData {
	uint32_t flags = 0;
	bool on_top = false;
	bool alpha_scissor = false;
	bool supported = false;

	void set_code(const String &p_code) override {
		flags = 0;
		on_top = p_code.contains("depth_test_disabled");
		alpha_scissor = p_code.contains("ALPHA_SCISSOR_THRESHOLD =");
		supported = true;
		// Shader resources send preprocessed source with comments stripped.
		if (p_code.contains("camera_world_pos_on_plane") && p_code.contains("grid_size")) {
			flags = GRID | VERTEX_COLOR | SRGB_VERTEX_COLOR;
		} else if (p_code.contains("screen_point_final") && p_code.contains("clip_final")) {
			flags = ORIGIN_AXIS;
		} else if (on_top && p_code.contains("orientation <= 0.005") && p_code.contains("VERTEX += NORMAL")) {
			flags = AXIS_RING;
		} else if (on_top && p_code.contains("camera_dir_local") && p_code.contains("rotation_matrix")) {
			flags = VIEW_RING;
		} else if (p_code.contains("Shader automatically converted from") && (p_code.contains("StandardMaterial3D.") || p_code.contains("ORMMaterial3D."))) {
			if (p_code.contains("albedo_tex *= COLOR")) {
				flags |= VERTEX_COLOR;
			}
			if (p_code.contains("Vertex Color is sRGB: Enabled")) {
				flags |= SRGB_VERTEX_COLOR;
			}
			if (p_code.contains("Billboard Mode: Enabled") || p_code.contains("Billboard Mode: Y-Billboard")) {
				flags |= BILLBOARD;
			}
			if (p_code.contains("Fixed Size: Enabled")) {
				flags |= FIXED_SIZE;
			}
			if (p_code.contains("POINT_SIZE = point_size")) {
				flags |= POINT_SPRITE;
			}
		} else {
			supported = false;
		}
	}

	bool is_animated() const override { return false; }
	bool casts_shadows() const override { return true; }
	RenderingServerTypes::ShaderNativeSourceCode get_native_source_code() const override { return {}; }
	Pair<ShaderRD *, RID> get_native_shader_and_version() const override { return {}; }
};

struct GizmoMaterialData : MaterialStorage::MaterialData {
	const GizmoShaderData *shader;
	int32_t priority = 0;

	explicit GizmoMaterialData(MaterialStorage::ShaderData *p_shader) :
			shader(static_cast<GizmoShaderData *>(p_shader)) {}
	void set_render_priority(int p_priority) override { priority = p_priority; }
	void set_next_pass(RID p_pass) override {}
	bool update_parameters(const HashMap<StringName, Variant> &p_parameters, bool p_uniform_dirty, bool p_textures_dirty) override { return false; }
};

void store_color(const Color &p_color, float (&r_data)[4]) {
	const Color linear = p_color.srgb_to_linear();
	for (int i = 0; i < 4; i++) {
		r_data[i] = linear[i];
	}
}

} // namespace

EditorGizmoPass::EditorGizmoPass() {
	auto *materials = MaterialStorage::get_singleton();
	materials->shader_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, []() -> MaterialStorage::ShaderData * {
		return memnew(GizmoShaderData);
	});
	materials->material_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, [](MaterialStorage::ShaderData *p_shader) -> MaterialStorage::MaterialData * {
		return memnew(GizmoMaterialData(p_shader));
	});
}

EditorGizmoPass::~EditorGizmoPass() {
	auto *materials = MaterialStorage::get_singleton();
	materials->shader_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, nullptr);
	materials->material_set_data_request_function(MaterialStorage::SHADER_TYPE_3D, nullptr);
	RD *rd = RD::get_singleton();
	if (white_texture.is_valid()) {
		rd->free_rid(white_texture);
	}
	if (frame_buffer.is_valid()) {
		rd->free_rid(frame_buffer);
	}
}

void EditorGizmoPass::add(const RenderGeometryInstanceBase &p_instance, const Transform3D &p_camera) {
	const auto &data = *p_instance.data;
	if (data.base_type == RSE::INSTANCE_MESH) {
		_add_mesh(p_instance, data.base, p_instance.transform, p_camera, Color(1, 1, 1));
	} else if (data.base_type == RSE::INSTANCE_MULTIMESH) {
		// The grid origin uses a small MultiMesh of twelve colored line segments.
		auto *meshes = MeshStorage::get_singleton();
		const RID mesh = meshes->multimesh_get_mesh(data.base);
		if (mesh.is_null() || meshes->multimesh_get_transform_format(data.base) != RSE::MULTIMESH_TRANSFORM_3D) {
			return;
		}
		const bool uses_colors = meshes->multimesh_uses_colors(data.base);
		const uint32_t count = meshes->multimesh_get_instances_to_draw(data.base);
		for (uint32_t i = 0; i < count; i++) {
			const Transform3D transform = p_instance.transform * meshes->multimesh_instance_get_transform(data.base, i);
			const Color color = uses_colors ? meshes->multimesh_instance_get_color(data.base, i) : Color(1, 1, 1);
			_add_mesh(p_instance, mesh, transform, p_camera, color);
		}
	}
}

void EditorGizmoPass::_add_mesh(const RenderGeometryInstanceBase &p_instance, RID p_mesh, const Transform3D &p_transform, const Transform3D &p_camera, const Color &p_modulation) {
	auto *meshes = MeshStorage::get_singleton();
	auto *materials = MaterialStorage::get_singleton();
	auto *textures = TextureStorage::get_singleton();
	const auto &overrides = *p_instance.data;

	const int surface_count = meshes->mesh_get_surface_count(p_mesh);
	for (int i = 0; i < surface_count; i++) {
		void *surface = meshes->mesh_get_surface(p_mesh, uint32_t(i));
		const uint64_t format = meshes->mesh_surface_get_format(surface);
		if (!(format & RSE::ARRAY_FORMAT_VERTEX) || (format & RSE::ARRAY_FLAG_USE_2D_VERTICES) || meshes->mesh_surface_get_vertices_drawn_count(surface) == 0) {
			continue;
		}

		RID material = overrides.material_override;
		if (material.is_null() && i < overrides.surface_materials.size()) {
			material = overrides.surface_materials[i];
		}
		if (material.is_null()) {
			material = meshes->mesh_surface_get_material(p_mesh, i);
		}
		const auto *material_data = static_cast<const GizmoMaterialData *>(materials->material_get_data(material, MaterialStorage::SHADER_TYPE_3D));
		if (!material_data || !material_data->shader->supported) {
			continue;
		}

		const GizmoShaderData &style = *material_data->shader;
		const Variant albedo_param = materials->material_get_param(material, SNAME("albedo"));
		const Color albedo = albedo_param.get_type() == Variant::COLOR ? Color(albedo_param) : Color(1, 1, 1);
		if (albedo.a <= 0 || p_modulation.a <= 0) {
			continue;
		}

		DrawItem item;
		item.surface = surface;
		item.on_top = style.on_top;
		item.priority = material_data->priority;
		item.depth = p_camera.basis.get_column(2).dot(p_camera.origin - p_transform.origin);
		item.data.flags = style.flags;
		item.data.instance.set_transform(p_transform, p_transform.basis);
		if (format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) {
			item.data.instance.set_compressed_aabb(meshes->mesh_surface_get_aabb(surface));
			const Vector4 uv_scale = meshes->mesh_surface_get_uv_scale(surface);
			item.data.uv_scale[0] = uv_scale.x;
			item.data.uv_scale[1] = uv_scale.y;
		}
		if (!(format & RSE::ARRAY_FORMAT_COLOR)) {
			item.data.flags &= ~(VERTEX_COLOR | SRGB_VERTEX_COLOR);
		}

		switch (meshes->mesh_surface_get_primitive(surface)) {
			case RSE::PRIMITIVE_TRIANGLES:
				item.primitive = TRIANGLES;
				break;
			case RSE::PRIMITIVE_LINES:
				item.primitive = LINES;
				break;
			case RSE::PRIMITIVE_LINE_STRIP:
				item.primitive = LINE_STRIP;
				break;
			case RSE::PRIMITIVE_POINTS:
				item.primitive = POINTS;
				if ((style.flags & POINT_SPRITE) && !(format & RSE::ARRAY_FORMAT_INDEX)) {
					item.primitive = TRIANGLES;
					item.point_instances = meshes->mesh_surface_get_vertex_count(surface);
					item.data.point_size = MAX(float(materials->material_get_param(material, SNAME("point_size"))), 1.0f);
				}
				break;
			default:
				continue;
		}
		if (item.point_instances == 0) {
			item.data.flags &= ~POINT_SPRITE;
		}
		if (style.flags & GRID) {
			item.data.grid_size = MAX(float(materials->material_get_param(material, SNAME("grid_size"))), 0.001f);
		}
		if (style.alpha_scissor) {
			const Variant threshold = materials->material_get_param(material, SNAME("alpha_scissor_threshold"));
			item.data.alpha_scissor = threshold.is_num() ? float(threshold) : 0.5f;
		}
		RID albedo_texture = materials->material_get_param(material, SNAME("texture_albedo"));
		if (albedo_texture.is_null()) {
			albedo_texture = materials->material_get_param(material, SNAME("albedo_texture"));
		}
		item.texture = textures->texture_get_rd_texture(albedo_texture, true);
		store_color(albedo, item.data.albedo);
		store_color(p_modulation, item.data.modulation);
		draws.push_back(item);
	}
}

bool EditorGizmoPass::_prepare_shader() {
	const uint64_t version = shader.get_version();
	if (!shader.is_loaded()) {
		shader.load("editor_gizmo.slang");
	}
	shader.poll_hot_reload();
	if (shader.get_version() != version) {
		const RD::RenderPrimitive primitives[] = { RD::RENDER_PRIMITIVE_TRIANGLES, RD::RENDER_PRIMITIVE_LINES, RD::RENDER_PRIMITIVE_LINESTRIPS, RD::RENDER_PRIMITIVE_POINTS };
		RD::PipelineColorBlendState blend = RD::PipelineColorBlendState::create_blend();
		blend.attachments.write[0].src_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
		for (int primitive = 0; primitive < PRIMITIVE_COUNT; primitive++) {
			for (int on_top = 0; on_top < 2; on_top++) {
				RD::PipelineDepthStencilState depth;
				depth.enable_depth_test = !on_top;
				depth.depth_compare_operator = RD::COMPARE_OP_GREATER_OR_EQUAL;
				pipelines[primitive][on_top].setup(shader.get_shader(), primitives[primitive], {}, {}, depth, blend);
			}
		}
	}
	return shader.get_shader().is_valid();
}

void EditorGizmoPass::render(const YutrelPipelineTargets &p_targets, const RenderSceneDataRD &p_scene, const YutrelFrameData &p_frame) {
	if (draws.is_empty() || !_prepare_shader()) {
		return;
	}

	RD *rd = RD::get_singleton();
	if (frame_buffer.is_null()) {
		frame_buffer = rd->uniform_buffer_create(sizeof(FrameData));
		ERR_FAIL_COND(frame_buffer.is_null());
		rd->set_resource_name(frame_buffer, "Yutrel Editor Gizmo Frame UBO");
	}
	if (white_texture.is_null()) {
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
		format.width = 1;
		format.height = 1;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
		white_texture = rd->texture_create(format, {}, Vector<Vector<uint8_t>>{ Vector<uint8_t>{ 255, 255, 255, 255 } });
		ERR_FAIL_COND(white_texture.is_null());
	}

	FrameData frame = {};
	memcpy(frame.view_projection, p_frame.view_projection, sizeof(frame.view_projection));
	for (int i = 0; i < 3; i++) {
		frame.camera_x[i] = p_scene.cam_transform.basis.rows[i][0];
		frame.camera_y[i] = p_scene.cam_transform.basis.rows[i][1];
		frame.camera_z[i] = p_scene.cam_transform.basis.rows[i][2];
		frame.camera_position[i] = p_scene.cam_transform.origin[i];
	}
	frame.inverse_viewport_size[0] = 1.0f / p_targets.region.size.x;
	frame.inverse_viewport_size[1] = 1.0f / p_targets.region.size.y;
	frame.orthogonal_scale = p_scene.cam_orthogonal ? Math::abs(1.0f / p_scene.cam_projection.columns[1].y) : 0.0f;
	frame.convert_to_srgb = !p_targets.output_linear;
	ERR_FAIL_COND(rd->buffer_update(frame_buffer, 0, sizeof(frame), &frame) != OK);

	draws.sort();
	instances.resize(draws.size());
	for (uint32_t i = 0; i < draws.size(); i++) {
		instances[i] = draws[i].data;
	}
	ERR_FAIL_COND(!instance_buffer.upload(instances, "Yutrel Editor Gizmo SSBO"));

	auto *uniform_sets = UniformSetCacheRD::get_singleton();
	const RID shader_rid = shader.get_shader();
	const RID frame_set = uniform_sets->get_cache(shader_rid, 0,
			RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, frame_buffer),
			RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, instance_buffer.get_buffer()));
	const RID framebuffer = FramebufferCacheRD::get_singleton()->get_cache(p_targets.output_color, p_targets.scene_depth);
	ERR_FAIL_COND(framebuffer.is_null() || frame_set.is_null());

	const RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	const auto framebuffer_format = rd->framebuffer_get_format(framebuffer);
	const uint64_t input_mask = rd->shader_get_vertex_input_attribute_mask(shader_rid);
	auto *meshes = MeshStorage::get_singleton();
	for (DrawItem &item : draws) {
		RD::VertexFormatID vertex_format;
		meshes->mesh_surface_get_vertex_arrays_and_format(item.surface, input_mask, false, item.point_instances != 0, item.vertex_array, vertex_format);
		item.index_array = meshes->mesh_surface_get_index_array(item.surface, 0);
		item.pipeline = pipelines[item.primitive][item.on_top].get_render_pipeline(vertex_format, framebuffer_format);
		item.uniform_set = uniform_sets->get_cache(shader_rid, 1,
				RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, item.texture.is_valid() ? item.texture : white_texture),
				RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 1, sampler));
	}

	rd->draw_command_begin_label("Yutrel EditorGizmoPass", Color(0.9, 0.6, 0.2));
	const RD::DrawListID list = rd->draw_list_begin(framebuffer, RD::DRAW_DEFAULT_ALL, {}, 0.0f, 0, p_targets.region);
	rd->draw_list_bind_uniform_set(list, frame_set, 0);
	RID bound_material_set;
	for (uint32_t draw_id = 0; draw_id < draws.size(); draw_id++) {
		const DrawItem &item = draws[draw_id];
		if (item.pipeline.is_null() || item.uniform_set.is_null() || item.vertex_array.is_null()) {
			continue;
		}
		rd->draw_list_bind_render_pipeline(list, item.pipeline);
		if (item.uniform_set != bound_material_set) {
			rd->draw_list_bind_uniform_set(list, item.uniform_set, 1);
			bound_material_set = item.uniform_set;
		}
		rd->draw_list_bind_vertex_array(list, item.vertex_array);
		rd->draw_list_set_push_constant(list, &draw_id, sizeof(draw_id));
		if (item.point_instances != 0) {
			rd->draw_list_draw(list, false, item.point_instances, 6);
		} else {
			if (item.index_array.is_valid()) {
				rd->draw_list_bind_index_array(list, item.index_array);
			}
			rd->draw_list_draw(list, item.index_array.is_valid());
		}
	}
	rd->draw_list_end();
	rd->draw_command_end_label();
}

} // namespace RendererSceneRenderImplementation

#endif // TOOLS_ENABLED
