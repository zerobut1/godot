/**************************************************************************/
/*  gltf_document_extension_yutrel.cpp                                    */
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

#include "gltf_document_extension_yutrel.h"

#ifdef MODULE_GLTF_ENABLED
#include "yutrel_material.h"

static Ref<Texture2D> get_gltf_texture(const Ref<GLTFState> &p_state, const Dictionary &p_info) {
	const Vector<Ref<GLTFTexture>> &textures = p_state->get_textures();
	const int index = p_info.get("index", -1);
	if (index < 0 || index >= textures.size() || textures[index].is_null()) {
		return Ref<Texture2D>();
	}

	const Vector<Ref<Texture2D>> &images = p_state->get_images();
	const int source = textures[index]->get_src_image();
	if (source < 0 || source >= images.size()) {
		return Ref<Texture2D>();
	}
	return images[source];
}

static Ref<YutrelMaterial> import_material(const Ref<GLTFState> &p_state, const Dictionary &p_material, const Ref<Material> &p_old_material, int p_index) {
	Ref<YutrelMaterial> material;
	material.instantiate();
	const String name = p_material.get("name", "");
	material->set_name(name.is_empty() ? vformat("material_%s", itos(p_index)) : name);

	const Dictionary extensions = p_material.get("extensions", Dictionary());
	if (extensions.has("KHR_materials_unlit")) {
		material->set_shading_model(YutrelMaterial::SHADING_MODEL_UNLIT);
	}
	const Dictionary strength = extensions.get("KHR_materials_emissive_strength", Dictionary());
	if (strength.has("emissiveStrength")) {
		material->set_emissive_strength(strength["emissiveStrength"]);
	}

	const Ref<BaseMaterial3D> converted = p_old_material;
	if (extensions.has("KHR_materials_pbrSpecularGlossiness") && converted.is_valid()) {
		// GLTFDocument already converted this legacy workflow, including its textures.
		material->set_base_color(converted->get_albedo());
		material->set_base_color_texture(converted->get_texture(BaseMaterial3D::TEXTURE_ALBEDO));
		material->set_metallic(converted->get_metallic());
		material->set_metallic_texture(converted->get_texture(BaseMaterial3D::TEXTURE_METALLIC));
		material->set_roughness(converted->get_roughness());
		material->set_roughness_texture(converted->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS));
	} else {
		const Dictionary pbr = p_material.get("pbrMetallicRoughness", Dictionary());
		const Array color = pbr.get("baseColorFactor", Array());
		if (color.size() == 4) {
			material->set_base_color(Color(color[0], color[1], color[2], color[3]).linear_to_srgb());
		}
		const Dictionary base_texture = pbr.get("baseColorTexture", Dictionary());
		material->set_base_color_texture(get_gltf_texture(p_state, base_texture));
		const Dictionary texture_extensions = base_texture.get("extensions", Dictionary());
		const Dictionary transform = texture_extensions.get("KHR_texture_transform", Dictionary());
		const Array offset = transform.get("offset", Array());
		if (offset.size() == 2) {
			material->set_uv_offset(Vector2(offset[0], offset[1]));
		}
		const Array scale = transform.get("scale", Array());
		if (scale.size() == 2) {
			material->set_uv_scale(Vector2(scale[0], scale[1]));
		}

		material->set_metallic(pbr.get("metallicFactor", 1.0));
		material->set_roughness(pbr.get("roughnessFactor", 1.0));
		const Ref<Texture2D> metal_rough = get_gltf_texture(p_state, pbr.get("metallicRoughnessTexture", Dictionary()));
		material->set_metallic_texture(metal_rough);
		material->set_roughness_texture(metal_rough);
	}
	material->set_metallic_channel(YutrelMaterial::TEXTURE_CHANNEL_BLUE);
	material->set_roughness_channel(YutrelMaterial::TEXTURE_CHANNEL_GREEN);

	const Dictionary normal = p_material.get("normalTexture", Dictionary());
	material->set_normal_texture(get_gltf_texture(p_state, normal));
	material->set_normal_scale(normal.get("scale", 1.0));
	const Dictionary occlusion = p_material.get("occlusionTexture", Dictionary());
	material->set_occlusion_texture(get_gltf_texture(p_state, occlusion));
	material->set_occlusion_strength(occlusion.get("strength", 1.0));
	material->set_occlusion_channel(YutrelMaterial::TEXTURE_CHANNEL_RED);

	const Array emission = p_material.get("emissiveFactor", Array());
	if (emission.size() == 3) {
		material->set_emissive_color(Color(emission[0], emission[1], emission[2]).linear_to_srgb());
	}
	material->set_emissive_texture(get_gltf_texture(p_state, p_material.get("emissiveTexture", Dictionary())));

	const String alpha_mode = p_material.get("alphaMode", "OPAQUE");
	if (alpha_mode == "MASK") {
		material->set_alpha_mode(YutrelMaterial::ALPHA_MODE_MASK);
		material->set_alpha_cutoff(p_material.get("alphaCutoff", 0.5));
	} else if (alpha_mode == "BLEND") {
		material->set_alpha_mode(YutrelMaterial::ALPHA_MODE_BLEND);
	}
	if (p_material.get("doubleSided", false)) {
		material->set_cull_mode(YutrelMaterial::CULL_MODE_DISABLED);
	}
	const Variant extras = p_material.get("extras", Variant());
	if (extras.get_type() == Variant::DICTIONARY) {
		material->set_meta("extras", extras);
	}
	return material;
}

Error GLTFDocumentExtensionYutrel::import_post_parse(Ref<GLTFState> p_state) {
	if (p_state.is_null() || p_state->get_discard_meshes_and_materials()) {
		return OK;
	}

	const Dictionary json = p_state->get_json();
	const Array materials = json.get("materials", Array());
	const Vector<Ref<Material>> old_materials = p_state->get_materials();
	ERR_FAIL_COND_V(old_materials.size() != materials.size(), ERR_INVALID_DATA);
	HashMap<const Material *, Ref<YutrelMaterial>> remap;
	Vector<Ref<Material>> new_materials;
	for (int i = 0; i < materials.size(); i++) {
		const Ref<YutrelMaterial> material = import_material(p_state, materials[i], old_materials[i], i);
		remap.insert(old_materials[i].ptr(), material);
		new_materials.push_back(material);
	}
	p_state->set_materials(new_materials);

	for (const Ref<GLTFMesh> &gltf_mesh : p_state->get_meshes()) {
		const Ref<ImporterMesh> mesh = gltf_mesh->get_mesh();
		if (mesh.is_null()) {
			continue;
		}
		TypedArray<Material> instance_materials = gltf_mesh->get_instance_materials();
		instance_materials.resize(mesh->get_surface_count());
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			const Ref<Material> old = mesh->get_surface_material(s);
			const Ref<YutrelMaterial> *mapped = remap.getptr(old.ptr());
			Ref<YutrelMaterial> material;
			if (mapped) {
				material = *mapped;
			} else {
				// A primitive without a material uses glTF's white, fully metallic default.
				material.instantiate();
				material->set_metallic(1.0f);
			}

			const Ref<BaseMaterial3D> base = old;
			const Array arrays = mesh->get_surface_arrays(s);
			const Variant &colors = arrays[Mesh::ARRAY_COLOR];
			if ((base.is_valid() && base->get_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR)) ||
					(colors.get_type() == Variant::PACKED_COLOR_ARRAY && !PackedColorArray(colors).is_empty())) {
				material->set_vertex_color_use_as_albedo(true);
				material->set_vertex_color_is_srgb(false);
			}
			mesh->set_surface_material(s, material);
			instance_materials[s] = material;
		}
		gltf_mesh->set_instance_materials(instance_materials);
	}
	return OK;
}
#endif // MODULE_GLTF_ENABLED
