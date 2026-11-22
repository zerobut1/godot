/**************************************************************************/
/*  render_yutrel.cpp                                                     */
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

#include "render_yutrel.h"

#include "yutrel_render_data.h"
#include "yutrel_viewport_settings.h"

#include "core/string/print_string.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/utilities.h"

#ifdef TOOLS_ENABLED
#include "core/config/engine.h"
#endif

namespace RendererSceneRenderImplementation {

namespace {

class YutrelRenderBufferData : public RenderBufferCustomDataRD {
	GDCLASS(YutrelRenderBufferData, RenderBufferCustomDataRD);

public:
	RID gbuffer_a;
	RID gbuffer_b;
	RID gbuffer_c;
	RID shadow_mask;

	void configure(RenderSceneBuffersRD *p_buffers) override {
		if (p_buffers->get_view_count() != 1 || p_buffers->get_internal_size().x <= 0 || p_buffers->get_internal_size().y <= 0) {
			return;
		}
		constexpr uint32_t usage = RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
		gbuffer_a = p_buffers->create_texture(SNAME("Yutrel"), SNAME("GBuffer_A"), RD::DATA_FORMAT_R8G8B8A8_SRGB, usage);
		gbuffer_b = p_buffers->create_texture(SNAME("Yutrel"), SNAME("GBuffer_B"), RD::DATA_FORMAT_R16G16B16A16_UNORM, usage);
		shadow_mask = p_buffers->create_texture(SNAME("Yutrel"), SNAME("ShadowMask"), RD::DATA_FORMAT_R8_UNORM, usage);
		gbuffer_c = p_buffers->create_texture(SNAME("Yutrel"), SNAME("GBuffer_C"), RD::DATA_FORMAT_R8G8B8A8_UNORM, usage);
	}

	void free_data() override {
		// RenderSceneBuffersRD owns the textures created through its named storage.
		gbuffer_a = RID();
		gbuffer_b = RID();
		gbuffer_c = RID();
		shadow_mask = RID();
	}
};

} // namespace

void RenderYutrel::setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) {
	p_render_buffers->set_custom_data(SNAME("Yutrel"), Ref<YutrelRenderBufferData>(memnew(YutrelRenderBufferData)));
}

YutrelFrameData RenderYutrel::_build_frame_data(const RenderDataRD &p_data, const Rect2i &p_region) const {
	YutrelFrameData frame;
	const RenderSceneDataRD &scene = *p_data.scene_data;
	const Projection vp = scene.get_cam_projection() * Projection(scene.cam_transform.affine_inverse());
	const Projection inv_vp = vp.inverse();
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			frame.view_projection[row][column] = vp.columns[column][row];
			frame.inv_view_projection[row][column] = inv_vp.columns[column][row];
		}
	}
	const Vector3 to_camera = scene.cam_transform.basis.get_column(2).normalized();
	for (int axis = 0; axis < 3; axis++) {
		frame.camera_position[axis] = scene.cam_transform.origin[axis];
		frame.camera_forward[axis] = to_camera[axis];
	}
	frame.camera_forward[3] = scene.cam_orthogonal;
	frame.viewport_origin[0] = p_region.position.x;
	frame.viewport_origin[1] = p_region.position.y;
	frame.inv_viewport_size[0] = 1.0f / p_region.size.x;
	frame.inv_viewport_size[1] = 1.0f / p_region.size.y;

	return frame;
}

void RenderYutrel::_collect_geometry(const RenderDataRD &p_data) {
	base_pass.clear();
	transparent_pass.clear();
#ifdef TOOLS_ENABLED
	editor_gizmos.clear();
	const bool editor_view = Engine::get_singleton()->is_editor_hint();
	constexpr uint32_t scene_layer_mask = (1u << 20) - 1;
#endif
	if (!p_data.instances) {
		return;
	}

	const Transform3D &camera = p_data.scene_data->cam_transform;
	const bool camera_mirrored = camera.basis.determinant() < 0;
	const Vector3 camera_forward = camera.basis.get_column(2).normalized();
	for (uint32_t i = 0; i < p_data.instances->size(); i++) {
		const auto &instance = *static_cast<const RenderGeometryInstanceBase *>((*p_data.instances)[i]);
#ifdef TOOLS_ENABLED
		if (editor_view && !(instance.layer_mask & scene_layer_mask)) {
			editor_gizmos.add(instance, p_data.scene_data->cam_transform);
			continue;
		}
#endif
		base_pass.add(instance, camera_mirrored, material_bridge, transparent_pass, camera.origin, camera_forward);
	}
}

bool RenderYutrel::_get_native_targets(const RenderDataRD &p_data, YutrelPipelineTargets &r_targets) const {
	const Ref<RenderSceneBuffersRD> &buffers = p_data.render_buffers;
	auto *textures = RendererRD::TextureStorage::get_singleton();
	r_targets.render_target = buffers->get_render_target();
	ERR_FAIL_COND_V(r_targets.render_target.is_null(), false);
	const Size2i size = buffers->get_target_size();
	if (size.x <= 0 || size.y <= 0) {
		return false;
	}
	if (buffers->get_msaa_3d() != RSE::VIEWPORT_MSAA_DISABLED || textures->render_target_get_msaa(r_targets.render_target) != RSE::VIEWPORT_MSAA_DISABLED) {
		WARN_PRINT_ONCE("Yutrel requires MSAA to be disabled.");
		return false;
	}
	if (buffers->get_view_count() != 1 || p_data.scene_data->get_view_count() != 1 || buffers->get_use_taa()) {
		WARN_PRINT_ONCE("Yutrel requires a single view with TAA disabled.");
		return false;
	}
	if (buffers->get_internal_size() != size || textures->render_target_get_size(r_targets.render_target) != size) {
		WARN_PRINT_ONCE("Yutrel requires equal internal and output sizes.");
		return false;
	}

	const Rect2i full_region(Point2i(), size);
	r_targets.region = p_data.render_region == Rect2i() ? full_region : p_data.render_region.intersection(full_region);
	if (!r_targets.region.has_area()) {
		return false;
	}
	const Ref<YutrelRenderBufferData> custom = buffers->get_custom_data(SNAME("Yutrel"));
	ERR_FAIL_COND_V(custom.is_null(), false);
	r_targets.gbuffer_a = custom->gbuffer_a;
	r_targets.gbuffer_b = custom->gbuffer_b;
	r_targets.gbuffer_c = custom->gbuffer_c;
	r_targets.shadow_mask = custom->shadow_mask;
	r_targets.scene_color = buffers->get_internal_texture();
	r_targets.scene_depth = buffers->get_depth_texture();
	r_targets.output_color = textures->render_target_get_rd_texture(r_targets.render_target);
	ERR_FAIL_COND_V(r_targets.scene_color.is_null() || r_targets.scene_depth.is_null() || r_targets.output_color.is_null() ||
					r_targets.gbuffer_a.is_null() || r_targets.gbuffer_b.is_null() || r_targets.gbuffer_c.is_null() || r_targets.shadow_mask.is_null(),
			false);
	ERR_FAIL_COND_V(RD::get_singleton()->texture_get_format(r_targets.scene_color).format != RD::DATA_FORMAT_R16G16B16A16_SFLOAT, false);
	auto *framebuffers = FramebufferCacheRD::get_singleton();
	r_targets.base_framebuffer = framebuffers->get_cache(r_targets.scene_color, r_targets.gbuffer_a, r_targets.gbuffer_b, r_targets.gbuffer_c, r_targets.scene_depth);
	r_targets.lighting_framebuffer = framebuffers->get_cache(r_targets.scene_color);
	r_targets.output_framebuffer = framebuffers->get_cache(r_targets.output_color);
	r_targets.output_linear = textures->render_target_is_using_hdr(r_targets.render_target);
	return r_targets.base_framebuffer.is_valid() && r_targets.lighting_framebuffer.is_valid() && r_targets.output_framebuffer.is_valid();
}

bool RenderYutrel::_prepare_passes() {
	// Poll every shader even when another pass is unavailable.
	const bool caster_ready = csm_shadow_pass.prepare();
	const bool mask_ready = shadow_mask_pass.prepare();
	const bool base_ready = base_pass.prepare();
	const bool lighting_ready = directional_light_pass.prepare();
	ibl_pass.prepare();
	skybox_pass.prepare();
	const bool transparent_ready = transparent_pass.prepare();
	const bool output_ready = output_pass.prepare();
	const bool debug_ready = debug_view_pass.prepare();
	const bool dfg_ready = directional_light_pass.prepare_dfg_lut();
	return caster_ready && mask_ready && base_ready && lighting_ready && transparent_ready && output_ready && debug_ready && dfg_ready;
}

void RenderYutrel::_render_scene(RenderDataRD *p_render_data, const Color &p_default_color) {
	ERR_FAIL_NULL(p_render_data);
	if (p_render_data->reflection_probe.is_valid()) {
		return;
	}
	ERR_FAIL_COND(p_render_data->render_buffers.is_null());
	ERR_FAIL_NULL(p_render_data->scene_data);
	YutrelPipelineTargets targets;
	if (!_get_native_targets(*p_render_data, targets) || !_prepare_passes()) {
		return;
	}

	RD *rd = RD::get_singleton();
	const YutrelViewportSettings settings = YutrelViewportSettings::get(targets.render_target);

	const YutrelFrameData frame = _build_frame_data(*p_render_data, targets.region);
	_collect_geometry(*p_render_data);
	if (frame_buffer.is_null()) {
		frame_buffer = rd->uniform_buffer_create(sizeof(frame));
		ERR_FAIL_COND(frame_buffer.is_null());
		rd->set_resource_name(frame_buffer, "Yutrel Frame UBO");
	}
	ERR_FAIL_COND(rd->buffer_update(frame_buffer, 0, sizeof(frame), &frame) != OK);

	const YutrelEnvironmentSettings environment = YutrelEnvironmentSettings::get(p_render_data->environment);
	LocalVector<DirectionalLightItem> lights = DirectionalLightPass::collect(*p_render_data);
	ERR_FAIL_COND(!csm_shadow_pass.render(*p_render_data, lights, environment.shadow_resolution, material_bridge));
	if (!base_pass.render(targets, frame_buffer, p_default_color.srgb_to_linear())) {
		return;
	}

	const bool final = settings.debug_view == DebugViewMode::FINAL;
	const bool lighting_only = settings.debug_view == DebugViewMode::LIGHTING;
	const bool draw_lighting = (final || lighting_only) && base_pass.has_draws();
	const bool draw_transparent = (final || lighting_only) && transparent_pass.has_draws();
	const bool draw_sky = final && !p_render_data->transparent_bg;
	const YutrelSkyParams sky_params = {
		environment.sky_intensity,
		Math::sin(environment.sky_rotation_radians),
		Math::cos(environment.sky_rotation_radians),
	};
	const bool ibl_ready = (draw_lighting || draw_sky || draw_transparent) && ibl_pass.prepare_environment(environment);
	const bool debug_mask = settings.debug_view == DebugViewMode::SHADOW_MASK;
	const bool debug_cascades = settings.debug_view == DebugViewMode::CSM_CASCADES;
	bool primary_shadow = false;
	if (lighting_only && (!draw_lighting || lights.is_empty())) {
		rd->draw_list_begin(targets.lighting_framebuffer, RD::DRAW_CLEAR_COLOR_0, { Color(0, 0, 0, 0) }, 0.0f, 0, targets.region);
		rd->draw_list_end();
	}
	if (draw_lighting || debug_mask || debug_cascades) {
		for (uint32_t i = 0; i < lights.size(); i++) {
			const DirectionalLightItem &light = lights[i];
			if (light.params.has_shadow) {
				shadow_mask_pass.render(targets, frame_buffer, csm_shadow_pass.get_depth_array(), light.shadow, debug_cascades);
				primary_shadow = true;
				if (debug_mask || debug_cascades) {
					break;
				}
			}
			if (draw_lighting) {
				const RID mask = light.params.has_shadow ? targets.shadow_mask : material_bridge.get_white_texture();
				directional_light_pass.render(targets, frame_buffer, light.params, mask, lighting_only && i == 0);
			}
		}
	}
	if (draw_lighting && ibl_ready) {
		ibl_pass.render(targets, frame_buffer, directional_light_pass.get_dfg_lut(), sky_params);
	}
	if (draw_sky && ibl_ready) {
		skybox_pass.render(targets, frame_buffer, ibl_pass.get_source_cubemap(), ibl_pass.get_cube_sampler(), sky_params);
	}

	if (draw_transparent) {
		transparent_pass.render(targets, frame_buffer, lights, csm_shadow_pass.get_depth_array(), shadow_mask_pass.get_comparison_sampler(),
				directional_light_pass.get_dfg_lut(), ibl_pass, ibl_ready, sky_params);
	}

	if (final) {
		output_pass.render(targets, p_render_data->environment);
	} else {
		debug_view_pass.render(targets, settings.debug_view, primary_shadow);
	}

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		editor_gizmos.render(targets, *p_render_data->scene_data, frame);
	}
#endif
	RendererRD::TextureStorage::get_singleton()->render_target_disable_clear_request(targets.render_target);
}

RenderGeometryInstance *RenderYutrel::geometry_instance_create(RID p_base) {
	const RSE::InstanceType type = RendererRD::Utilities::get_singleton()->get_base_type(p_base);
	ERR_FAIL_COND_V(!((1 << type) & RSE::INSTANCE_GEOMETRY_MASK), nullptr);
	return memnew(GeometryInstanceYutrel(p_base, type));
}

void RenderYutrel::geometry_instance_free(RenderGeometryInstance *p_geometry_instance) {
	ERR_FAIL_NULL(p_geometry_instance);
	memdelete(static_cast<GeometryInstanceYutrel *>(p_geometry_instance));
}

RenderYutrel::RenderYutrel() {
	sky.set_texture_format(_render_buffers_get_preferred_color_format());
	directional_light_pass.initialize_dfg_lut();
	print_verbose("Yutrel: Slang deferred renderer created.");
}

RenderYutrel::~RenderYutrel() {
	RD *rd = RD::get_singleton();
	if (frame_buffer.is_valid()) {
		rd->free_rid(frame_buffer);
	}
	// This can call base_uniforms_changed(), so release before the base destructor.
	RendererRD::LightStorage::get_singleton()->directional_shadow_atlas_set_size(0);
}

} // namespace RendererSceneRenderImplementation
