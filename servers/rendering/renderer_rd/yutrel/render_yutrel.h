/**************************************************************************/
/*  render_yutrel.h                                                       */
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

#pragma once

#include "passes/base_pass.h"
#include "passes/csm_shadow_pass.h"
#include "passes/ddgi_pass.h"
#include "passes/debug_view_pass.h"
#include "passes/directional_light_pass.h"
#include "passes/ibl_pass.h"
#include "passes/output_pass.h"
#include "passes/path_tracer_pass.h"
#include "passes/shadow_mask_pass.h"
#include "passes/skybox_pass.h"
#include "passes/transparent_pass.h"
#include "yutrel_material.h"

#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"

#ifdef TOOLS_ENABLED
#include "passes/editor_gizmo_pass.h"
#endif

namespace RendererSceneRenderImplementation {

class RenderYutrel : public RendererSceneRenderRD {
	class GeometryInstanceYutrel : public RenderGeometryInstanceBase {
		Data instance_data;

	public:
		GeometryInstanceYutrel(RID p_base, RSE::InstanceType p_type) {
			data = &instance_data;
			data->base = p_base;
			data->base_type = p_type;
		}

		void _mark_dirty() override {}
		void set_use_lightmap(RID p_lightmap_instance, const Rect2 &p_lightmap_uv_scale, int p_lightmap_slice_index) override {}
		void set_lightmap_capture(const Color *p_sh9) override {}
		void clear_light_instances() override {}
		void pair_light_instance(const RID p_light_instance, RSE::LightType p_light_type, uint32_t p_placement_idx) override {}
		void pair_reflection_probe_instances(const RID *p_reflection_probe_instances, uint32_t p_reflection_probe_instance_count) override {}
		void pair_decal_instances(const RID *p_decal_instances, uint32_t p_decal_instance_count) override {}
		void pair_voxel_gi_instances(const RID *p_voxel_gi_instances, uint32_t p_voxel_gi_instance_count) override {}
		void set_softshadow_projector_pairing(bool p_softshadow, bool p_projector) override {}
	};

	YutrelMaterialBridge material_bridge;
	RID frame_buffer;
	BasePass base_pass;
	CSMShadowPass csm_shadow_pass;
	ShadowMaskPass shadow_mask_pass;
	DirectionalLightPass directional_light_pass;
	IBLPass ibl_pass;
	SkyboxPass skybox_pass;
	TransparentPass transparent_pass;
	OutputPass output_pass;
	DebugViewPass debug_view_pass;
	YutrelRayTracingScene rt_scene;
	PathTracerPass path_tracer_pass;
	DDGIPass ddgi_pass;
	bool runtime_path_tracing = false;
#ifdef TOOLS_ENABLED
	EditorGizmoPass editor_gizmos;
#endif

	YutrelFrameData _build_frame_data(const RenderDataRD &p_data, const Rect2i &p_region) const;
	void _collect_geometry(const RenderDataRD &p_data);
	bool _get_native_targets(const RenderDataRD &p_data, YutrelPipelineTargets &r_targets) const;
	bool _prepare_passes();

protected:
	void setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) override;
	void _render_scene(RenderDataRD *p_render_data, const Color &p_default_color) override;
	void _render_material(const Transform3D &p_cam_transform, const Projection &p_cam_projection, bool p_cam_orthogonal, const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region, float p_exposure_normalization) override {}
	void _render_uv2(const PagedArray<RenderGeometryInstance *> &p_instances, RID p_framebuffer, const Rect2i &p_region) override {}
	void _render_sdfgi(Ref<RenderSceneBuffersRD> p_render_buffers, const Vector3i &p_from, const Vector3i &p_size, const AABB &p_bounds, const PagedArray<RenderGeometryInstance *> &p_instances, const RID &p_albedo_texture, const RID &p_emission_texture, const RID &p_emission_aniso_texture, const RID &p_geom_facing_texture, float p_exposure_normalization) override {}
	void _render_particle_collider_heightfield(RID p_fb, const Transform3D &p_cam_transform, const Projection &p_cam_projection, const PagedArray<RenderGeometryInstance *> &p_instances) override {}
	RID _render_buffers_get_normal_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override { return RID(); }
	RID _render_buffers_get_velocity_texture(Ref<RenderSceneBuffersRD> p_render_buffers) override { return RID(); }

public:
	RenderGeometryInstance *geometry_instance_create(RID p_base) override;
	void geometry_instance_free(RenderGeometryInstance *p_geometry_instance) override;
	uint32_t geometry_instance_get_pair_mask() override { return 0; }
	uint32_t get_max_lights_total() override { return 0; }
	uint32_t get_max_lights_per_mesh() override { return 0; }
	void mesh_generate_pipelines(RID p_mesh, bool p_background_compilation) override {}
	uint32_t get_pipeline_compilations(RSE::PipelineSource p_source) override { return 0; }

	void sdfgi_update(const Ref<RenderSceneBuffers> &p_render_buffers, RID p_environment, const Vector3 &p_world_position) override {}
	int sdfgi_get_pending_region_count(const Ref<RenderSceneBuffers> &p_render_buffers) const override { return 0; }
	AABB sdfgi_get_pending_region_bounds(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override { return AABB(); }
	uint32_t sdfgi_get_pending_region_cascade(const Ref<RenderSceneBuffers> &p_render_buffers, int p_region) const override { return 0; }
	void environment_set_ssao_quality(RSE::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override {}
	void environment_set_ssil_quality(RSE::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to) override {}
	void environment_set_ssr_half_size(bool p_half_size) override {}
	void environment_set_ssr_roughness_quality(RSE::EnvironmentSSRRoughnessQuality p_quality) override {}
	void sub_surface_scattering_set_quality(RSE::SubSurfaceScatteringQuality p_quality) override {}
	void sub_surface_scattering_set_scale(float p_scale, float p_depth_scale) override {}
	void base_uniforms_changed() override {}
	bool is_dynamic_gi_supported() const override { return false; }
	bool is_volumetric_supported() const override { return false; }
	bool is_vrs_supported() const override { return false; }
	bool _render_buffers_can_be_storage() override { return false; }

	void enable_features(BitField<FeatureBits> p_feature_bits) override {}
	String get_name() const override { return "Yutrel"; }

	RenderYutrel();
	~RenderYutrel();
};

} // namespace RendererSceneRenderImplementation
