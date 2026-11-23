/**************************************************************************/
/*  yutrel_rt_scene.cpp                                                   */
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

#include "yutrel_rt_scene.h"

#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_scene_cull.h"
#include "servers/rendering/renderer_viewport.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

bool YutrelRayTracingScene::is_supported(uint32_t p_pass_textures) {
	RD *rd = RD::get_singleton();
	return rd->has_feature(RD::SUPPORTS_RAY_QUERY) &&
			rd->limit_get(RD::LIMIT_MAX_TEXTURES_PER_SHADER_STAGE) >= MAX_TEXTURES + p_pass_textures &&
			rd->limit_get(RD::LIMIT_MAX_TEXTURES_PER_UNIFORM_SET) >= MAX_TEXTURES + p_pass_textures;
}

RID YutrelRayTracingScene::get_scenario(const RenderDataRD &p_data) {
	for (const RendererViewport::Viewport *viewport : RSG::viewport->active_viewports) {
		if (viewport->render_buffers.ptr() == p_data.render_buffers.ptr()) {
			return viewport->scenario;
		}
	}
	return RID();
}

namespace {

struct SceneInstance {
	const RenderGeometryInstanceBase *geometry;
	bool shadows_only;

	bool operator<(const SceneInstance &p_other) const { return geometry < p_other.geometry; }
};

// A camera's raster draw list omits off-screen reflectors, emitters and occluders.
LocalVector<SceneInstance> collect_instances(const RenderDataRD &p_data) {
	LocalVector<SceneInstance> result;
	const auto *scenario = RendererSceneCull::singleton->scenario_owner.get_or_null(YutrelRayTracingScene::get_scenario(p_data));
	if (scenario) {
		using InstanceData = RendererSceneCull::InstanceData;
		const uint32_t layers = p_data.scene_data->camera_visible_layers & ((1u << 20) - 1);
		for (uint32_t i = 0; i < scenario->instance_data.size(); i++) {
			const InstanceData &data = scenario->instance_data[i];
			if ((data.flags & InstanceData::FLAG_BASE_TYPE_MASK) != RSE::INSTANCE_MESH || !(data.layer_mask & layers)) {
				continue;
			}
			result.push_back({ static_cast<const RenderGeometryInstanceBase *>(data.instance_geometry), bool(data.flags & InstanceData::FLAG_CAST_SHADOWS_ONLY) });
		}
		return result;
	}
	// Preview renders can supply a draw list without an associated viewport scenario.
	if (p_data.instances) {
		for (uint32_t i = 0; i < p_data.instances->size(); i++) {
			result.push_back({ static_cast<const RenderGeometryInstanceBase *>((*p_data.instances)[i]), false });
		}
	}
	return result;
}

struct RTMaterials {
	LocalVector<YutrelMaterialData> material_records;
	LocalVector<YutrelRTMaterialTextures> texture_records;
	HashMap<RID, int> material_ids;
	HashMap<RID, uint32_t> texture_ids;
	Vector<RID> table;

	explicit RTMaterials(RID p_white) {
		table.push_back(p_white);
		texture_ids.insert(p_white, 0);
	}

	int resolve(YutrelMaterialBridge &p_bridge, RID p_material) {
		if (const int *id = material_ids.getptr(p_material)) {
			return *id;
		}
		YutrelMaterialBridge::Material mat;
		int id = -1;
		if (p_bridge.get_material(p_material, mat) && !mat.transparent) {
			id = material_records.size();
			material_records.push_back(mat.data);
			YutrelRTMaterialTextures slots = {};
			for (int slot = 0; slot < YutrelMaterialBridge::TEXTURE_COUNT; slot++) {
				const RID texture = mat.textures[slot];
				if (const uint32_t *index = texture_ids.getptr(texture)) {
					slots.slots[slot] = *index;
				} else if (table.size() < YutrelRayTracingScene::MAX_TEXTURES) {
					slots.slots[slot] = table.size();
					texture_ids.insert(texture, table.size());
					table.push_back(texture);
				} else {
					WARN_PRINT_ONCE("Yutrel RT texture table exceeded 512 entries. Using white for excess textures.");
				}
			}
			texture_records.push_back(slots);
		}
		material_ids.insert(p_material, id);
		return id;
	}
};

} // namespace

struct YutrelRayTracingScene::MeshEntry {
	struct Surface {
		int mesh_surface;
		uint32_t first_index;
		uint32_t index_count;
	};

	LocalVector<Surface> surfaces;
	LocalVector<YutrelRTVertex> vertices;
	LocalVector<uint32_t> indices;
	uint32_t arena_vertex = 0;
	uint32_t arena_index = 0;
	RID vertex_buffer, index_buffer, blas;
	DependencyTracker dependency;
	bool dirty = false;

	bool build(RID p_mesh) {
		auto *storage = RendererRD::MeshStorage::get_singleton();
		dependency.userdata = this;
		dependency.changed_callback = [](Dependency::DependencyChangedNotification p_change, DependencyTracker *p_tracker) {
			if (p_change == Dependency::DEPENDENCY_CHANGED_MESH) {
				static_cast<MeshEntry *>(p_tracker->userdata)->dirty = true;
			}
		};
		dependency.update_dependency(storage->mesh_get_dependency(p_mesh));

		for (int i = 0; i < storage->mesh_get_surface_count(p_mesh); i++) {
			void *surface = storage->mesh_get_surface(p_mesh, uint32_t(i));
			const uint64_t format = storage->mesh_surface_get_format(surface);
			constexpr uint64_t required = RSE::ARRAY_FORMAT_VERTEX | RSE::ARRAY_FORMAT_NORMAL;
			constexpr uint64_t unsupported = RSE::ARRAY_FLAG_USE_2D_VERTICES | RSE::ARRAY_FORMAT_BONES | RSE::ARRAY_FORMAT_WEIGHTS;
			if ((format & required) != required || (format & unsupported) || storage->mesh_surface_get_primitive(surface) != RSE::PRIMITIVE_TRIANGLES) {
				continue;
			}
			const auto sd = static_cast<const RendererRD::MeshStorage *>(storage)->mesh_get_surface(p_mesh, i);
			const uint32_t count = sd.index_count ? sd.index_count : sd.vertex_count;
			if (!count || count % 3) {
				continue;
			}
			// The engine decoder handles both packed TBN encodings, UV scale, UNORM colors and index widths.
			const Array arrays = RS::get_singleton()->mesh_create_arrays_from_surface_data(sd);
			ERR_FAIL_COND_V(arrays.size() != RSE::ARRAY_MAX, false);
			const PackedVector3Array positions = arrays[RSE::ARRAY_VERTEX], normals = arrays[RSE::ARRAY_NORMAL];
			const PackedFloat32Array tangents = arrays[RSE::ARRAY_TANGENT];
			const PackedColorArray colors = arrays[RSE::ARRAY_COLOR];
			const PackedVector2Array uvs = arrays[RSE::ARRAY_TEX_UV];
			const PackedInt32Array triangle_indices = arrays[RSE::ARRAY_INDEX];
			const uint32_t first_vertex = vertices.size();
			surfaces.push_back({ i, indices.size(), count });
			for (uint32_t j = 0; j < sd.vertex_count; j++) {
				YutrelRTVertex vertex = {};
				for (int axis = 0; axis < 3; axis++) {
					vertex.position[axis] = positions[j][axis];
					vertex.normal[axis] = normals[j][axis];
				}
				if (format & RSE::ARRAY_FORMAT_TANGENT) {
					memcpy(vertex.tangent, tangents.ptr() + j * 4, sizeof(vertex.tangent));
				}
				const Color color = colors.is_empty() ? Color(1, 1, 1, 1) : colors[j];
				memcpy(vertex.color, color.components, sizeof(vertex.color));
				if (!uvs.is_empty()) {
					vertex.u = uvs[j].x;
					vertex.v = uvs[j].y;
				}
				vertices.push_back(vertex);
			}
			for (uint32_t j = 0; j < count; j++) {
				const uint32_t index = triangle_indices.is_empty() ? j : uint32_t(triangle_indices[j]);
				ERR_FAIL_UNSIGNED_INDEX_V(index, sd.vertex_count, false);
				indices.push_back(first_vertex + index);
			}
		}
		if (surfaces.is_empty()) {
			return true;
		}

		RD *rd = RD::get_singleton();
		const BitField<RD::BufferCreationBits> usage = RD::BUFFER_CREATION_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT | RD::BUFFER_CREATION_DEVICE_ADDRESS_BIT;
		const uint32_t vertex_bytes = vertices.size() * sizeof(YutrelRTVertex);
		vertex_buffer = rd->vertex_buffer_create(vertex_bytes, { reinterpret_cast<const uint8_t *>(vertices.ptr()), vertex_bytes }, usage);
		index_buffer = rd->index_buffer_create(indices.size(), RD::INDEX_BUFFER_FORMAT_UINT32, { reinterpret_cast<const uint8_t *>(indices.ptr()), indices.size() * sizeof(uint32_t) }, false, usage);
		ERR_FAIL_COND_V(vertex_buffer.is_null() || index_buffer.is_null(), false);
		rd->set_resource_name(vertex_buffer, "Yutrel BLAS Vertices");
		rd->set_resource_name(index_buffer, "Yutrel BLAS Indices");
		LocalVector<RD::AccelerationStructureGeometry> geometries;
		for (const Surface &s : surfaces) {
			RD::AccelerationStructureGeometry geometry;
			geometry.vertex_buffer = vertex_buffer;
			geometry.vertex_stride = sizeof(YutrelRTVertex);
			geometry.vertex_format = RD::DATA_FORMAT_R32G32B32_SFLOAT;
			geometry.vertex_count = vertices.size();
			geometry.index_buffer = index_buffer;
			geometry.index_offset = s.first_index * sizeof(uint32_t);
			geometry.index_count = s.index_count;
			geometries.push_back(geometry);
		}
		blas = rd->blas_create(geometries, RD::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
		ERR_FAIL_COND_V(blas.is_null(), false);
		rd->set_resource_name(blas, "Yutrel Mesh BLAS");
		return rd->blas_build(blas) == OK;
	}

	~MeshEntry() {
		for (RID rid : { blas, index_buffer, vertex_buffer }) {
			if (rid.is_valid()) {
				RD::get_singleton()->free_rid(rid);
			}
		}
	}
};

bool YutrelRayTracingScene::_update_arena(const Vector<RID> &p_meshes) {
	Vector<RID> active_blas;
	for (RID rid : p_meshes) {
		active_blas.push_back(meshes[rid]->blas);
	}
	if (arena_blas == active_blas && vertices.get_buffer().is_valid()) {
		return true;
	}
	LocalVector<YutrelRTVertex> merged_vertices;
	LocalVector<uint32_t> merged_indices;
	for (RID rid : p_meshes) {
		MeshEntry &mesh = *meshes[rid];
		mesh.arena_vertex = merged_vertices.size();
		mesh.arena_index = merged_indices.size();
		for (const YutrelRTVertex &vertex : mesh.vertices) {
			merged_vertices.push_back(vertex);
		}
		for (uint32_t index : mesh.indices) {
			merged_indices.push_back(index);
		}
	}
	if (p_meshes.is_empty()) {
		merged_vertices.push_back({});
		merged_indices.push_back(0);
	}
	if (!vertices.upload(merged_vertices, "Yutrel RT Vertices") || !indices.upload(merged_indices, "Yutrel RT Indices")) {
		return false;
	}
	arena_blas = std::move(active_blas);
	return true;
}

bool YutrelRayTracingScene::update(const RenderDataRD &p_data, YutrelMaterialBridge &p_bridge) {
	auto *storage = RendererRD::MeshStorage::get_singleton();
	Vector<RID> stale;
	for (const KeyValue<RID, MeshEntry *> &entry : meshes) {
		if (entry.value->dirty || !storage->owns_mesh(entry.key)) {
			stale.push_back(entry.key);
		}
	}
	for (RID rid : stale) {
		tables_valid = false;
		memdelete(meshes[rid]);
		meshes.erase(rid);
	}

	LocalVector<RD::AccelerationStructureInstance> builds;
	LocalVector<YutrelRTInstance> records;
	LocalVector<YutrelRTSurface> surface_records;
	LocalVector<RID> instance_meshes;
	HashSet<RID> used;
	RTMaterials frame_materials(p_bridge.get_white_texture());

	uint64_t hash = UINT64_C(14695981039346656037);
	// Keep indices stable when the native culler reorders its instance list.
	LocalVector<SceneInstance> ordered = collect_instances(p_data);
	ordered.sort();
	for (const SceneInstance &candidate : ordered) {
		const RenderGeometryInstanceBase &instance = *candidate.geometry;
		const auto &data = *instance.data;
		const RID rid = data.base;
		const real_t det = instance.transform.basis.determinant();
		if (!(instance.layer_mask & ((1u << 20) - 1)) || data.base_type != RSE::INSTANCE_MESH || data.skeleton.is_valid() ||
				!Math::is_finite(det) || det == 0 || storage->mesh_get_blend_shape_count(rid)) {
			continue;
		}
		if (!meshes.has(rid)) {
			bool has_material = false;
			for (int j = 0; j < storage->mesh_get_surface_count(rid); j++) {
				if (frame_materials.resolve(p_bridge, YutrelMaterialBridge::get_surface_material(instance, rid, j)) >= 0) {
					has_material = true;
					break;
				}
			}
			if (!has_material) {
				continue;
			}
			MeshEntry *entry = memnew(MeshEntry);
			if (!entry->build(rid)) {
				memdelete(entry);
				return false;
			}
			meshes.insert(rid, entry);
		}
		const MeshEntry &mesh = *meshes[rid];
		YutrelRTInstance record = {};
		record.first_surface = surface_records.size();
		record.surface_count = mesh.surfaces.size();
		bool any_hit = candidate.shadows_only;
		bool included = false;
		for (const MeshEntry::Surface &s : mesh.surfaces) {
			const int id = frame_materials.resolve(p_bridge, YutrelMaterialBridge::get_surface_material(instance, rid, s.mesh_surface));
			included |= id >= 0;
			any_hit |= id < 0 || (frame_materials.material_records[id].flags & (YutrelMaterialData::ALPHA_SCISSOR | YutrelMaterialData::CULL_FRONT));
			surface_records.push_back({ 0, s.first_index, uint32_t(MAX(0, id)), uint32_t(id < 0) });
		}
		if (!included) {
			surface_records.resize(record.first_surface);
			continue;
		}

		record.flags = uint32_t(det < 0) | (uint32_t(candidate.shadows_only) << 1);
		const Basis normal = instance.transform.basis.inverse().transposed();
		RendererRD::MaterialStorage::store_transform_transposed_3x4(instance.transform, record.model[0]);
		for (int row = 0; row < 3; row++) {
			for (int col = 0; col < 3; col++) {
				record.normal[row][col] = normal[row][col];
			}
		}
		RD::AccelerationStructureInstance build;
		build.transform = instance.transform;
		build.blas = mesh.blas;
		build.id = records.size();
		build.mask = 0xff;
		build.hit_sbt_range = int64_t(1) << 32; // RD requires a nonempty range even for inline queries.
		build.flags = RD::ACCELERATION_STRUCTURE_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT |
				(any_hit ? RD::ACCELERATION_STRUCTURE_INSTANCE_FORCE_NO_OPAQUE_BIT : RD::ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT);
		builds.push_back(build);
		records.push_back(record);
		used.insert(rid);
		instance_meshes.push_back(rid);
		hash = hash_djb2_one_64(rid.get_id(), hash);
		hash = hash_djb2_one_64(mesh.blas.get_id(), hash);
	}

	Vector<RID> active;
	for (RID mesh : used) {
		active.push_back(mesh);
	}
	active.sort();
	if (!_update_arena(active)) {
		tables_valid = false;
		return false;
	}
	for (uint32_t i = 0; i < records.size(); i++) {
		const MeshEntry &mesh = *meshes[instance_meshes[i]];
		for (uint32_t j = 0; j < records[i].surface_count; j++) {
			YutrelRTSurface &s = surface_records[records[i].first_surface + j];
			s.vertex_offset = mesh.arena_vertex;
			s.index_offset += mesh.arena_index;
		}
	}
	hash = rt_hash_bytes(hash, records.ptr(), records.size() * sizeof(YutrelRTInstance));
	hash = rt_hash_bytes(hash, surface_records.ptr(), surface_records.size() * sizeof(YutrelRTSurface));
	hash = rt_hash_bytes(hash, frame_materials.material_records.ptr(), frame_materials.material_records.size() * sizeof(YutrelMaterialData));
	hash = rt_hash_bytes(hash, frame_materials.texture_records.ptr(), frame_materials.texture_records.size() * sizeof(YutrelRTMaterialTextures));
	for (RID texture : frame_materials.table) {
		hash = hash_djb2_one_64(texture.get_id(), hash);
	}
	if (tables_valid && hash == scene_hash) {
		return true;
	}

	RD *rd = RD::get_singleton();
	tables_valid = false;
	if (capacity < MAX(1u, builds.size())) {
		if (tlas.is_valid()) {
			rd->free_rid(tlas);
		}
		capacity = Math::next_power_of_2(MAX(1u, builds.size()));
		tlas = rd->tlas_create(capacity, RD::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
		if (tlas.is_null()) {
			capacity = 0;
			return false;
		}
		rd->set_resource_name(tlas, "Yutrel Scene TLAS");
	}
	ERR_FAIL_COND_V(rd->tlas_build(tlas, builds) != OK, false);
	has_instances = !records.is_empty();
	if (!has_instances) {
		// Bind real descriptors even when the shader only evaluates the sky.
		records.push_back({});
		surface_records.push_back({});
		frame_materials.material_records.push_back({});
		frame_materials.texture_records.push_back({});
	}
	if (!instances.upload(records, "Yutrel RT Instances") || !surfaces.upload(surface_records, "Yutrel RT Surfaces") ||
			!materials.upload(frame_materials.material_records, "Yutrel RT Materials") || !material_textures.upload(frame_materials.texture_records, "Yutrel RT Material Textures")) {
		return false;
	}
	if (textures != frame_materials.table) {
		_free_sets();
		textures = frame_materials.table;
	}
	scene_hash = hash;
	tables_valid = true;
	return true;
}

void YutrelRayTracingScene::_free_sets() {
	RD *rd = RD::get_singleton();
	for (const ShaderSet &entry : shader_sets) {
		if (rd->uniform_set_is_valid(entry.set)) {
			rd->free_rid(entry.set);
		}
	}
	shader_sets.clear();
}

RID YutrelRayTracingScene::get_uniform_set(RID p_shader) {
	RD *rd = RD::get_singleton();
	// DDGI and RTAO share the TLAS in the same frame. Keep both descriptor sets
	// alive, pruning dependencies invalidated by shader reloads or buffer growth.
	for (uint32_t i = shader_sets.size(); i > 0; --i) {
		const ShaderSet &entry = shader_sets[i - 1];
		if (!rd->uniform_set_is_valid(entry.set)) {
			shader_sets.remove_at_unordered(i - 1);
		} else if (entry.shader == p_shader) {
			return entry.set;
		}
	}
	Vector<RD::Uniform> bindings = { RD::Uniform(RD::UNIFORM_TYPE_ACCELERATION_STRUCTURE, 0, tlas) };
	for (RID buffer : { vertices.get_buffer(), indices.get_buffer(), instances.get_buffer(), surfaces.get_buffer(), materials.get_buffer(), material_textures.get_buffer() }) {
		bindings.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, bindings.size(), buffer));
	}
	const RID sampler = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR_WITH_MIPMAPS, RSE::CANVAS_ITEM_TEXTURE_REPEAT_ENABLED);
	bindings.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 7, sampler));
	RD::Uniform images;
	images.uniform_type = RD::UNIFORM_TYPE_TEXTURE;
	images.binding = 8;
	for (int i = 0; i < int(MAX_TEXTURES); i++) {
		images.append_id(i < textures.size() ? textures[i] : textures[0]);
	}
	bindings.push_back(images);
	const RID set = rd->uniform_set_create(bindings, p_shader, 1);
	if (set.is_valid()) {
		shader_sets.push_back({ p_shader, set });
	}
	return set;
}

YutrelRayTracingScene::~YutrelRayTracingScene() {
	_free_sets();
	if (tlas.is_valid()) {
		RD::get_singleton()->free_rid(tlas);
	}
	for (const KeyValue<RID, MeshEntry *> &entry : meshes) {
		memdelete(entry.value);
	}
}

} // namespace RendererSceneRenderImplementation
