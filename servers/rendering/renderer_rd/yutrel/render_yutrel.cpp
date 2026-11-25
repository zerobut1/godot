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

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/string/print_string.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#ifdef TOOLS_ENABLED
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server_default.h"
#endif
#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/utilities.h"
#include "servers/rendering/rendering_server_globals.h"

namespace RendererSceneRenderImplementation {

namespace {

class YutrelRenderBufferData : public RenderBufferCustomDataRD {
	GDCLASS(YutrelRenderBufferData, RenderBufferCustomDataRD);

public:
	RID path_tracer_accum;
	DDGIPass::History ddgi_history;
	RTAOPass::History rtao_history;
	RTReflectionPass::History rt_reflection_history;
	RTShadowPass::History rt_shadow_history;
	uint64_t pt_state_hash = 0;
	uint32_t pt_sample_count = 0;

	AutoExposurePass::History auto_exposure_history;
	BloomPass::History bloom_history;
	OutputPass::LutCache grading_lut;
	UpscalerPass::History upscaler_history;
	bool has_prev_camera = false;
	bool prev_orthogonal = false;
	bool prev_upscaler_active = false;
	Projection prev_unjittered_vp;
	Transform3D prev_cam_transform;
	Projection prev_cam_projection;
	Vector2 prev_jitter_uv;
	Rect2i prev_region;
	Size2i prev_render_size;
	DebugViewMode prev_view = DebugViewMode::FINAL;
	UpscalerMode prev_mode = UpscalerMode::OFF;
	UpscalerQuality prev_quality = UpscalerQuality::NATIVE_AA;
	uint32_t jitter_index = 0;
#ifdef TOOLS_ENABLED
	uint32_t converged_frames = 0;
	uint64_t prev_scene_hash = 0;
#endif

	void configure(RenderSceneBuffersRD *p_buffers) override {}

	void free_data() override {
		// RenderSceneBuffersRD releases all named render- and display-size textures.
		has_prev_camera = false;
		prev_render_size = Size2i();
		jitter_index = 0;
		auto_exposure_history.clear();
		bloom_history.clear();
		grading_lut.clear();
		upscaler_history.clear();
		path_tracer_accum = RID();
		pt_sample_count = 0;
		pt_state_hash = 0;
		ddgi_history.clear();
		rtao_history.clear();
		rt_reflection_history.clear();
		rt_shadow_history.clear();
	}
};

} // namespace

void RenderYutrel::setup_render_buffer_data(Ref<RenderSceneBuffersRD> p_render_buffers) {
	p_render_buffers->set_custom_data(SNAME("Yutrel"), Ref<YutrelRenderBufferData>(memnew(YutrelRenderBufferData)));
}

static YutrelFrameData build_frame_data(const RenderSceneDataRD &scene, const Rect2i &p_region, const Projection &vp,
		const Projection &unjittered, const Projection &prev_unjittered, Vector2 jitter_uv, Vector2 prev_jitter_uv) {
	YutrelFrameData frame;
	const Projection inv_vp = vp.inverse();
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			frame.unjittered_view_projection[row][column] = unjittered.columns[column][row];
			frame.prev_unjittered_view_projection[row][column] = prev_unjittered.columns[column][row];
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
	frame.jitter_uv[0] = jitter_uv.x;
	frame.jitter_uv[1] = jitter_uv.y;
	frame.prev_jitter_uv[0] = prev_jitter_uv.x;
	frame.prev_jitter_uv[1] = prev_jitter_uv.y;
	frame.viewport_origin[0] = p_region.position.x;
	frame.viewport_origin[1] = p_region.position.y;
	frame.inv_viewport_size[0] = 1.0f / p_region.size.x;
	frame.inv_viewport_size[1] = 1.0f / p_region.size.y;

	return frame;
}

void RenderYutrel::GeometryInstanceYutrel::set_transform(const Transform3D &p_transform, const AABB &p_aabb, const AABB &p_transformed_aabb) {
	const uint64_t frame = RSG::rasterizer->get_frame_number();
	if (!has_transform || frame != prev_transform_change_frame) {
		prev_transform = has_transform ? transform : p_transform;
		teleported = !has_transform;
	} else if (teleported) {
		prev_transform = p_transform;
	}
	prev_transform_change_frame = frame;
	has_transform = true;
	RenderGeometryInstanceBase::set_transform(p_transform, p_aabb, p_transformed_aabb);
}

void RenderYutrel::GeometryInstanceYutrel::reset_motion_vectors() {
	prev_transform = transform;
	teleported = true;
	prev_transform_change_frame = RSG::rasterizer->get_frame_number();
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

	const uint64_t frame = RSG::rasterizer->get_frame_number();
	const Transform3D &camera = p_data.scene_data->cam_transform;
	const bool camera_mirrored = camera.basis.determinant() < 0;
	const Vector3 camera_forward = camera.basis.get_column(2).normalized();
	for (uint32_t i = 0; i < p_data.instances->size(); i++) {
		const auto &instance = *static_cast<const GeometryInstanceYutrel *>((*p_data.instances)[i]);
#ifdef TOOLS_ENABLED
		if (editor_view && !(instance.layer_mask & scene_layer_mask)) {
			editor_gizmos.add(instance, p_data.scene_data->cam_transform);
			continue;
		}
#endif
		base_pass.add(instance, camera_mirrored, material_bridge, transparent_pass, camera.origin, camera_forward, instance.get_prev_transform(frame));
	}
}

bool RenderYutrel::_prepare_targets(const RenderDataRD &p_data, bool p_enable_upscaler, UpscalerQuality p_quality, YutrelPipelineTargets &r_targets) const {
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
	r_targets.output_region = p_data.render_region == Rect2i() ? full_region : p_data.render_region.intersection(full_region);
	if (!r_targets.output_region.has_area()) {
		return false;
	}
	const bool active = p_enable_upscaler && r_targets.output_region == full_region;
	const float scale = active ? UpscalerPass::get_render_scale(p_quality) : 1.0f;
	const Size2i render_size(MAX(1, int(Math::round(size.x * scale))), MAX(1, int(Math::round(size.y * scale))));
	r_targets.region = active ? Rect2i(Point2i(), render_size) : r_targets.output_region;
	const Ref<YutrelRenderBufferData> custom = buffers->get_custom_data(SNAME("Yutrel"));
	ERR_FAIL_COND_V(custom.is_null(), false);
	const StringName &scope = SNAME("Yutrel");
	if (custom->prev_render_size != render_size) {
		buffers->clear_context(scope);
		custom->path_tracer_accum = RID();
		custom->pt_sample_count = 0;
	}
	constexpr uint32_t usage = RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	const auto texture = [&](const StringName &p_name, RD::DataFormat p_format, uint32_t p_usage) {
		return buffers->create_texture(scope, p_name, p_format, p_usage, RD::TEXTURE_SAMPLES_1, render_size);
	};
	r_targets.gbuffer_a = texture(SNAME("GBuffer_A"), RD::DATA_FORMAT_R8G8B8A8_SRGB, usage);
	r_targets.gbuffer_b = texture(SNAME("GBuffer_B"), RD::DATA_FORMAT_R16G16B16A16_UNORM, usage);
	r_targets.gbuffer_c = texture(SNAME("GBuffer_C"), RD::DATA_FORMAT_R8G8B8A8_UNORM, usage);
	r_targets.velocity = texture(SNAME("Velocity"), RD::DATA_FORMAT_R16G16_SFLOAT, usage);
	r_targets.reactive_mask = texture(SNAME("ReactiveMask"), RD::DATA_FORMAT_R8_UNORM, usage | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT);
	r_targets.shadow_mask = texture(SNAME("ShadowMask"), RD::DATA_FORMAT_R16_SFLOAT, usage | RD::TEXTURE_USAGE_STORAGE_BIT);
	r_targets.output_depth = buffers->get_depth_texture();
	r_targets.scene_depth = r_targets.output_depth;
	r_targets.scene_color = buffers->get_internal_texture();
	if (size != render_size) {
		r_targets.scene_color = texture(SNAME("SceneColor"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage | RD::TEXTURE_USAGE_STORAGE_BIT);
		r_targets.scene_depth = texture(SNAME("SceneDepth"), RD::DATA_FORMAT_D32_SFLOAT, RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT);
	}
	if (active) {
		r_targets.upscaled_color = buffers->create_texture(SNAME("YutrelDisplay"), SNAME("UpscaledColor"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage | RD::TEXTURE_USAGE_STORAGE_BIT);
		ERR_FAIL_COND_V(r_targets.upscaled_color.is_null(), false);
	} else if (custom->prev_upscaler_active) {
		custom->upscaler_history.clear();
		buffers->clear_context(SNAME("YutrelDisplay"));
	}
	r_targets.output_color = textures->render_target_get_rd_texture(r_targets.render_target);
	ERR_FAIL_COND_V(r_targets.scene_color.is_null() || r_targets.scene_depth.is_null() || r_targets.output_color.is_null() ||
					r_targets.gbuffer_a.is_null() || r_targets.velocity.is_null() || r_targets.reactive_mask.is_null() || r_targets.gbuffer_b.is_null() || r_targets.gbuffer_c.is_null() || r_targets.shadow_mask.is_null(),
			false);
	ERR_FAIL_COND_V(RD::get_singleton()->texture_get_format(r_targets.scene_color).format != RD::DATA_FORMAT_R16G16B16A16_SFLOAT, false);
	auto *framebuffers = FramebufferCacheRD::get_singleton();
	r_targets.base_framebuffer = framebuffers->get_cache(r_targets.scene_color, r_targets.gbuffer_a, r_targets.gbuffer_b, r_targets.gbuffer_c, r_targets.velocity, r_targets.scene_depth);
	r_targets.lighting_framebuffer = framebuffers->get_cache(r_targets.scene_color);
	r_targets.output_framebuffer = framebuffers->get_cache(r_targets.output_color);
	r_targets.output_linear = textures->render_target_is_using_hdr(r_targets.render_target);
	return r_targets.base_framebuffer.is_valid() && r_targets.lighting_framebuffer.is_valid() && r_targets.output_framebuffer.is_valid();
}

RID RenderYutrel::_render_buffers_get_velocity_texture(Ref<RenderSceneBuffersRD> p_buffers) {
	return p_buffers->has_texture(SNAME("Yutrel"), SNAME("Velocity")) ? p_buffers->get_texture(SNAME("Yutrel"), SNAME("Velocity")) : RID();
}

void RenderYutrel::_copy_output_depth(const YutrelPipelineTargets &p_targets, Vector2 p_jitter_px) {
	if (refresh_shader(depth_copy_shader, "depth_copy.slang")) {
		RD::PipelineDepthStencilState depth;
		depth.enable_depth_test = true;
		depth.enable_depth_write = true;
		depth.depth_compare_operator = RD::COMPARE_OP_ALWAYS;
		depth_copy_pipeline.setup(depth_copy_shader.get_shader(), RD::RENDER_PRIMITIVE_TRIANGLES, {}, {}, depth, RD::PipelineColorBlendState::create_disabled(0));
	}
	ERR_FAIL_COND(depth_copy_shader.get_shader().is_null());
	const RID fb = FramebufferCacheRD::get_singleton()->get_cache(p_targets.output_depth);
	const RID set = UniformSetCacheRD::get_singleton()->get_cache(depth_copy_shader.get_shader(), 0, RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 0, p_targets.scene_depth));
	const Vector2 scale = Vector2(p_targets.region.size) / Vector2(p_targets.output_region.size);
	const float params[] = { float(scale.x), float(scale.y), float(p_jitter_px.x), float(p_jitter_px.y) };
	draw_fullscreen("Yutrel Display Depth Copy", Color(0.3, 0.4, 0.7), p_targets.output_region, fb, depth_copy_pipeline, set, params, sizeof(params));
}

bool RenderYutrel::_prepare_passes() {
	// Poll every shader even when another pass is unavailable.
	const bool mask_ready = shadow_mask_pass.prepare();
	const bool base_ready = base_pass.prepare();
	const bool lighting_ready = directional_light_pass.prepare();
	ibl_pass.prepare();
	skybox_pass.prepare();
	const bool transparent_ready = transparent_pass.prepare();
	const bool output_ready = output_pass.prepare();
	const bool debug_ready = debug_view_pass.prepare();
	const bool dfg_ready = directional_light_pass.prepare_dfg_lut();
	return mask_ready && base_ready && lighting_ready && transparent_ready && output_ready && debug_ready && dfg_ready;
}

void RenderYutrel::_render_scene(RenderDataRD *p_render_data, const Color &p_default_color) {
	ERR_FAIL_NULL(p_render_data);
	if (p_render_data->reflection_probe.is_valid()) {
		return;
	}
	ERR_FAIL_COND(p_render_data->render_buffers.is_null());
	ERR_FAIL_NULL(p_render_data->scene_data);
	RD *rd = RD::get_singleton();
	const RID target = p_render_data->render_buffers->get_render_target();
	YutrelViewportSettings settings = YutrelViewportSettings::get(target, runtime_path_tracing ? DebugViewMode::PATH_TRACER : DebugViewMode::FINAL);
	if (get_debug_draw_mode() == RSE::VIEWPORT_DEBUG_DRAW_MOTION_VECTORS) {
		settings.debug_view = DebugViewMode::MOTION_VECTORS;
	}
	if (settings.debug_view == DebugViewMode::PATH_TRACER && !PathTracerPass::is_supported()) {
		WARN_PRINT_ONCE("Yutrel Path Tracer requires ray queries and non-uniform indexing of 2049 sampled images per stage. Falling back to Final.");
		settings.debug_view = DebugViewMode::FINAL;
	}
	if (is_rt_surface_debug_view(settings.debug_view) && !YutrelRayTracingScene::is_supported(0)) {
		WARN_PRINT_ONCE("Yutrel RT Surface views require ray queries and a 2048-image material table. Falling back to Final.");
		settings.debug_view = DebugViewMode::FINAL;
	}
	const bool rt_surface_debug = is_rt_surface_debug_view(settings.debug_view);

	const YutrelEnvironmentSettings environment = YutrelEnvironmentSettings::get(p_render_data->environment);
	const Ref<YutrelRenderBufferData> custom = p_render_data->render_buffers->get_custom_data(SNAME("Yutrel"));
	const RenderSceneDataRD &scene = *p_render_data->scene_data;
	YutrelPipelineTargets targets;
	const bool enable = environment.upscaler_mode != UpscalerMode::OFF && settings.debug_view == DebugViewMode::FINAL && !scene.cam_orthogonal;
	if (!_prepare_targets(*p_render_data, enable, environment.upscaler_quality, targets)) {
		return;
	}
	if (!environment.auto_exposure_enabled) {
		custom->auto_exposure_history.clear();
	}
	if (!environment.bloom_enabled || environment.bloom_intensity <= 0.0f) {
		custom->bloom_history.clear();
	}
	const bool active = targets.upscaled_color.is_valid();
	const Size2i render_size = targets.region.size;
	const Size2i display_size = targets.output_region.size;
	const bool special_view = rt_surface_debug || settings.debug_view == DebugViewMode::PATH_TRACER;
	const bool reset = !custom->has_prev_camera || custom->prev_region != targets.region || custom->prev_orthogonal != scene.cam_orthogonal ||
			custom->prev_mode != environment.upscaler_mode || custom->prev_quality != environment.upscaler_quality ||
			custom->prev_upscaler_active != active || ((special_view || is_rt_surface_debug_view(custom->prev_view) ||
					custom->prev_view == DebugViewMode::PATH_TRACER) && settings.debug_view != custom->prev_view);
	if (reset) {
		custom->jitter_index = 0;
		custom->rtao_history.has_history = false;
		custom->rt_reflection_history.has_history = false;
		custom->rt_shadow_history.invalidate();
	}
	const uint32_t phases = active ? UpscalerPass::get_phase_count(render_size, display_size) : 1;
	const Vector2 jitter_px = active ? UpscalerPass::get_jitter(custom->jitter_index++, phases) : Vector2();
	const Vector2 jitter_uv = jitter_px / Vector2(render_size);
	Projection correction;
	correction.set_depth_correction(scene.flip_y);
	const Projection view(scene.cam_transform.affine_inverse());
	const Projection unjittered = (correction * scene.cam_projection) * view;
	correction.add_jitter_offset(2 * jitter_uv);
	const Projection projection = correction * scene.cam_projection;
	const YutrelFrameData frame = build_frame_data(scene, targets.region, projection * view, unjittered,
			reset ? unjittered : custom->prev_unjittered_vp, jitter_uv, reset ? jitter_uv : custom->prev_jitter_uv);
	const UpscalerPass::Camera camera = { scene.cam_projection, reset ? scene.cam_projection : custom->prev_cam_projection,
		scene.cam_transform, reset ? scene.cam_transform : custom->prev_cam_transform, jitter_px, scene.z_near, scene.z_far, scene.time_step, reset };
#ifdef TOOLS_ENABLED
	const bool stationary_camera = !reset && unjittered == custom->prev_unjittered_vp;
#endif
	custom->prev_cam_transform = scene.cam_transform;
	custom->prev_cam_projection = scene.cam_projection;
	custom->prev_unjittered_vp = unjittered;
	custom->prev_jitter_uv = jitter_uv;
	custom->prev_region = targets.region;
	custom->prev_render_size = active ? render_size : p_render_data->render_buffers->get_target_size();
	custom->prev_orthogonal = scene.cam_orthogonal;
	custom->prev_mode = environment.upscaler_mode;
	custom->prev_quality = environment.upscaler_quality;
	custom->prev_view = settings.debug_view;
	custom->prev_upscaler_active = active;
	custom->has_prev_camera = false;
	material_bridge.set_mip_lod_bias(active ? std::log2(float(render_size.x) / display_size.x) - 0.5f : 0.0f);
	if (frame_buffer.is_null()) {
		frame_buffer = rd->uniform_buffer_create(sizeof(frame));
		ERR_FAIL_COND(frame_buffer.is_null());
		rd->set_resource_name(frame_buffer, "Yutrel Frame UBO");
	}
	{
		const YutrelScopedLabel upload_label("Yutrel Frame Upload", Color(0.4, 0.45, 0.55));
		ERR_FAIL_COND(rd->buffer_update(frame_buffer, 0, sizeof(frame), &frame) != OK);
	}

	if (environment.shadow_mode != ShadowMode::RAY_TRACED && custom->rt_shadow_history.raw_shadow.is_valid()) {
		custom->rt_shadow_history.clear();
	}
	if (!environment.rtao_enabled && custom->rtao_history.ao_texture.is_valid()) {
		custom->rtao_history.clear();
	}
	if (!environment.rt_reflection_enabled && custom->rt_reflection_history.get_output().is_valid()) {
		custom->rt_reflection_history.clear();
	}
	LocalVector<DirectionalLightItem> lights;
	if (!rt_surface_debug) {
		ERR_FAIL_COND(!local_light_pass.collect(*p_render_data));
		lights = DirectionalLightPass::collect(*p_render_data);
	}
	if (settings.debug_view == DebugViewMode::PATH_TRACER) {
		custom->rt_shadow_history.invalidate();
		custom->rt_reflection_history.has_history = false;
		custom->rtao_history.has_history = false;
		ERR_FAIL_COND(!output_pass.prepare() || !ibl_pass.prepare_environment(environment));
#ifdef TOOLS_ENABLED
		if (Engine::get_singleton()->is_editor_hint()) {
			ERR_FAIL_COND(!base_pass.prepare() || !transparent_pass.prepare());
			RENDER_TIMESTAMP("Yutrel BasePass");
			_collect_geometry(*p_render_data);
			ERR_FAIL_COND(!base_pass.render(targets, frame_buffer, p_default_color.srgb_to_linear(), p_render_data->render_info));
		}
#endif
		if (custom->path_tracer_accum.is_null()) {
			custom->path_tracer_accum = p_render_data->render_buffers->create_texture(SNAME("Yutrel"), SNAME("PathTracerAccum"),
					RD::DATA_FORMAT_R32G32B32A32_SFLOAT, RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT);
		}
		targets.path_tracer_accum = custom->path_tracer_accum;
		RENDER_TIMESTAMP("Yutrel RT Scene Update");
		ERR_FAIL_COND(targets.path_tracer_accum.is_null() || !rt_scene.update(*p_render_data, material_bridge));
		RENDER_TIMESTAMP("Yutrel Path Tracer");
		ERR_FAIL_COND(!path_tracer_pass.render(targets, frame_buffer, frame, lights, local_light_pass, environment, ibl_pass, rt_scene, custom->pt_state_hash, custom->pt_sample_count));
		output_pass.render(targets, environment, targets.path_tracer_accum);
#ifdef TOOLS_ENABLED
		if (Engine::get_singleton()->is_editor_hint()) {
			RENDER_TIMESTAMP("Yutrel Editor Gizmos");
			editor_gizmos.render(targets, *p_render_data->scene_data, frame);
		}
#endif
		RendererRD::TextureStorage::get_singleton()->render_target_disable_clear_request(targets.render_target);
		return;
	}
	if (rt_surface_debug) {
		// Geometry collection resolves material sets for both opaque and transparent batches.
		ERR_FAIL_COND(!base_pass.prepare() || !transparent_pass.prepare());
	} else if (!_prepare_passes()) {
		return;
	}
	RENDER_TIMESTAMP("Yutrel Collect Geometry");
	_collect_geometry(*p_render_data);
	const bool probes = settings.debug_view == DebugViewMode::DDGI_PROBES;
	const bool indirect_only = settings.debug_view == DebugViewMode::DDGI_INDIRECT;
	const bool final = settings.debug_view == DebugViewMode::FINAL || probes;
	const bool lighting_only = settings.debug_view == DebugViewMode::LIGHTING;
	const bool draw_lighting = (final || lighting_only) && base_pass.has_draws();
	const bool draw_transparent = (final || lighting_only) && transparent_pass.has_draws();
	const bool debug_mask = settings.debug_view == DebugViewMode::SHADOW_MASK;
	const bool debug_cascades = settings.debug_view == DebugViewMode::CSM_CASCADES;
	const bool transparent_rt_supported = draw_transparent && YutrelRayTracingScene::is_supported(TransparentPass::RT_SAMPLED_IMAGES);
	const bool want_rt_shadow = environment.shadow_mode == ShadowMode::RAY_TRACED && !debug_cascades &&
			YutrelRayTracingScene::is_supported(2) && (!draw_transparent || transparent_rt_supported);
	bool has_shadow_light = false;
	for (const DirectionalLightItem &light : lights) {
		if (light.shadow_enabled) {
			has_shadow_light = true;
			break;
		}
	}
	if (!want_rt_shadow && !rt_surface_debug) {
		RENDER_TIMESTAMP("Yutrel CSM Shadows");
		ERR_FAIL_COND(!csm_shadow_pass.prepare() || !csm_shadow_pass.render(*p_render_data, lights, environment.shadow_resolution, material_bridge));
	}
	RENDER_TIMESTAMP("Yutrel BasePass");
	if (!base_pass.render(targets, frame_buffer, p_default_color.srgb_to_linear(), p_render_data->render_info)) {
		return;
	}

	const bool draw_sky = final && !p_render_data->transparent_bg;
	const YutrelSkyParams sky_params = {
		environment.sky_intensity,
		Math::sin(environment.sky_rotation_radians),
		Math::cos(environment.sky_rotation_radians),
	};
	YutrelDDGISettings volume;
	const bool has_volume = YutrelDDGISettings::get(YutrelRayTracingScene::get_scenario(*p_render_data), volume);
	const bool rt_reflection_mode = settings.debug_view == DebugViewMode::RT_REFLECTION;
	const bool needs_rt_reflection = environment.rt_reflection_enabled && (final || lighting_only || rt_reflection_mode) &&
			base_pass.has_draws() && YutrelRayTracingScene::is_supported(RTReflectionPass::RT_SAMPLED_IMAGES);
	const bool needs_ddgi = has_volume && volume.size[volume.size.min_axis_index()] > 0 &&
			(final || lighting_only || indirect_only || needs_rt_reflection) && YutrelRayTracingScene::is_supported(4);
	const bool rtao_mode = settings.debug_view == DebugViewMode::RTAO;
	const bool needs_rtao = environment.rtao_enabled && (final || lighting_only || indirect_only || rtao_mode) &&
			base_pass.has_draws() && YutrelRayTracingScene::is_supported(2);
	const bool ibl_ready = (needs_rt_reflection || needs_ddgi || draw_lighting || draw_sky || draw_transparent) && ibl_pass.prepare_environment(environment);
	bool rt_ready = false;
	const bool needs_rt_shadow = want_rt_shadow && has_shadow_light && (draw_lighting || draw_transparent || debug_mask);
	const bool opaque_local_rt = draw_lighting && local_light_pass.has_shadows() && YutrelRayTracingScene::is_supported(LocalLightPass::RT_SAMPLED_IMAGES);
	const bool transparent_local_rt = transparent_rt_supported && local_light_pass.has_shadows();
	const bool needs_local_rt_shadow = opaque_local_rt || transparent_local_rt;
	if (needs_ddgi || needs_rtao || needs_rt_shadow || needs_rt_reflection || needs_local_rt_shadow || rt_surface_debug) {
		RENDER_TIMESTAMP("Yutrel RT Scene Update");
		rt_ready = rt_scene.update(*p_render_data, material_bridge);
	}
	bool ddgi_ready = false;
	if (needs_ddgi && ibl_ready && rt_ready) {
		RENDER_TIMESTAMP("> Yutrel DDGI");
		ddgi_ready = ddgi_pass.update(custom->ddgi_history, volume, lights, local_light_pass, ibl_pass, sky_params, rt_scene, p_render_data->scene_data->time_step);
		RENDER_TIMESTAMP("< Yutrel DDGI");
	}
	if (!has_volume && custom->ddgi_history.bindings.volume.is_valid()) {
		custom->ddgi_history.clear();
	}
	bool rtao_ready = false;
	if (needs_rtao && rt_ready) {
		RENDER_TIMESTAMP("> Yutrel RTAO");
		rtao_ready = rtao_pass.render(custom->rtao_history, targets, frame_buffer, frame, environment, rt_scene, p_render_data->scene_data->time_step);
		RENDER_TIMESTAMP("< Yutrel RTAO");
	}
	if (!rtao_ready) {
		custom->rtao_history.has_history = false;
	}
	targets.ao_texture = rtao_ready ? custom->rtao_history.ao_texture : rtao_pass.get_fallback_ao();
	DDGIBindings bindings;
	if (ddgi_ready) {
		bindings = custom->ddgi_history.bindings;
	} else if (draw_lighting || draw_transparent || needs_rt_reflection) {
		bindings = ibl_pass.get_inactive_ddgi();
	}
	bool rt_reflection_ready = false;
	if (needs_rt_reflection && rt_ready && ibl_ready) {
		RENDER_TIMESTAMP("> Yutrel RT Reflection");
		rt_reflection_ready = rt_reflection_pass.render(custom->rt_reflection_history, targets, frame_buffer, frame, environment, lights, local_light_pass,
				ibl_pass, sky_params, bindings, rt_scene, p_render_data->scene_data->time_step);
		RENDER_TIMESTAMP("< Yutrel RT Reflection");
	}
	if (!rt_reflection_ready) {
		custom->rt_reflection_history.has_history = false;
	}
	targets.rt_reflection_texture = rt_reflection_ready ? custom->rt_reflection_history.get_output() : rt_reflection_pass.get_fallback_reflection();
	bool primary_shadow = false;
	bool ran_rt_shadow = false;
	const bool draw_local = draw_lighting && local_light_pass.get_count() > 0;
	if (indirect_only || (lighting_only && (!draw_lighting || (lights.is_empty() && !draw_local)))) {
		const YutrelScopedLabel clear_label("Yutrel Clear Lighting", Color(0.3, 0.3, 0.4));
		rd->draw_list_begin(targets.lighting_framebuffer, RD::DRAW_CLEAR_COLOR_0, { Color(0, 0, 0, indirect_only ? 1 : 0) }, 0.0f, 0, targets.region);
		rd->draw_list_end();
	}
	if (draw_lighting || debug_mask || debug_cascades) {
		for (uint32_t i = 0; i < lights.size(); i++) {
			DirectionalLightItem &light = lights[i];
			bool rt_mask = false;
			if (needs_rt_shadow && rt_ready && light.shadow_enabled) {
				RENDER_TIMESTAMP("> Yutrel RT Shadow");
				rt_mask = rt_shadow_pass.render(custom->rt_shadow_history, light, targets, frame_buffer, frame, environment, rt_scene, p_render_data->scene_data->time_step);
				RENDER_TIMESTAMP("< Yutrel RT Shadow");
				ran_rt_shadow |= rt_mask;
			}
			light.params.has_shadow = rt_mask || light.shadow.cascade_count > 0;
			if (light.params.has_shadow) {
				if (!rt_mask) {
					RENDER_TIMESTAMP("Yutrel Shadow Mask");
					shadow_mask_pass.render(targets, frame_buffer, csm_shadow_pass.get_depth_array(), light.shadow, debug_cascades);
				}
				primary_shadow = true;
				if (debug_mask || debug_cascades) {
					break;
				}
			}
			if (draw_lighting) {
				RENDER_TIMESTAMP("Yutrel Direct Lighting");
				const RID mask = light.params.has_shadow ? targets.shadow_mask : material_bridge.get_white_texture();
				directional_light_pass.render(targets, frame_buffer, light.params, mask, lighting_only && i == 0);
			}
		}
	}
	if (draw_local || draw_transparent) {
		ERR_FAIL_COND(!local_light_pass.build_clusters(*p_render_data, targets.region, projection));
	}
	if (draw_local) {
		local_light_pass.render(targets, frame_buffer, directional_light_pass.get_dfg_lut(), opaque_local_rt && rt_ready ? &rt_scene : nullptr, lighting_only && lights.is_empty());
	}
	if (ran_rt_shadow) {
		custom->rt_shadow_history.finish_frame(frame);
	} else {
		custom->rt_shadow_history.invalidate();
	}
	if ((draw_lighting || (indirect_only && ddgi_ready && base_pass.has_draws())) && ibl_ready) {
		RENDER_TIMESTAMP("Yutrel IBL / Indirect");
		ibl_pass.render(targets, frame_buffer, directional_light_pass.get_dfg_lut(), sky_params, bindings, environment.rt_reflection_max_roughness, indirect_only);
	}
	if (!rt_surface_debug) {
		RENDER_TIMESTAMP("Yutrel Skybox");
		skybox_pass.render(targets, frame_buffer, draw_sky && ibl_ready ? ibl_pass.get_source_cubemap() : RID(), ibl_pass.get_cube_sampler(), sky_params);
	}

	if (draw_transparent) {
		RENDER_TIMESTAMP("Yutrel Transparent");
		transparent_pass.render(targets, frame_buffer, lights, local_light_pass, (needs_rt_shadow || transparent_local_rt) && rt_ready ? &rt_scene : nullptr, csm_shadow_pass.get_depth_array(), shadow_mask_pass.get_comparison_sampler(),
				directional_light_pass.get_dfg_lut(), ibl_pass, ibl_ready, sky_params, bindings, p_render_data->render_info);
	}

	if (active) {
		RENDER_TIMESTAMP("Yutrel Temporal Upscale");
		ERR_FAIL_COND(!upscaler_pass.render(custom->upscaler_history, targets, camera, environment, draw_transparent, custom->auto_exposure_history.get_exposure_texture()));
	}
#ifdef TOOLS_ENABLED
	if ((active || (final && environment.auto_exposure_enabled)) && Engine::get_singleton()->is_editor_hint()) {
		uint64_t hash = hash_djb2_one_64(environment.revision, local_light_pass.get_hash());
		hash = hash_djb2_one_64(ibl_pass.get_bake_input_hash(), hash);
		hash = hash_djb2_one_64(volume.config_version, hash);
		if (rt_ready) {
			hash = hash_djb2_one_64(rt_scene.get_scene_hash(), hash);
		}
		hash = base_pass.get_render_hash(hash);
		hash = transparent_pass.get_render_hash(hash);
		for (const auto &light : lights) {
			hash = rt_hash_bytes(hash, &light.params, sizeof(light.params));
		}
		const uint32_t convergence_frames = environment.auto_exposure_enabled ? MAX(90u, phases) : phases;
		custom->converged_frames = stationary_camera && hash == custom->prev_scene_hash ? MIN(custom->converged_frames + 1, convergence_frames) : 0;
		custom->prev_scene_hash = hash;
		if (custom->converged_frames < convergence_frames && DisplayServer::get_singleton() && DisplayServer::get_singleton()->window_is_focused()) {
			RenderingServerDefault::redraw_request();
		}
	}
#endif
	bool copy_depth = probes;
#ifdef TOOLS_ENABLED
	copy_depth |= editor_gizmos.has_draws();
#endif
	if (targets.scene_depth != targets.output_depth && copy_depth) {
		RENDER_TIMESTAMP("Yutrel Display Depth Copy");
		_copy_output_depth(targets, jitter_px);
	}

	if (final || indirect_only) {
		const RID hdr = active ? targets.upscaled_color : targets.scene_color;
		RID exposure;
		if (final && environment.auto_exposure_enabled) {
			ERR_FAIL_COND(!auto_exposure_pass.render(custom->auto_exposure_history, hdr, targets.output_region, environment, scene.time_step));
			exposure = custom->auto_exposure_history.get_exposure_texture();
		}
		RID bloom;
		if (final && environment.bloom_enabled && environment.bloom_intensity > 0.0f) {
			bloom = bloom_pass.render(custom->bloom_history, hdr, exposure, targets.output_region, environment);
			ERR_FAIL_COND(bloom.is_null());
		}
		output_pass.render(targets, environment, hdr, final ? &custom->grading_lut : nullptr, exposure, bloom);
		if (probes && ddgi_ready) {
			RENDER_TIMESTAMP("Yutrel DDGI Probes");
			ddgi_pass.draw_probes(targets, frame_buffer, custom->ddgi_history, environment);
		}
	} else if (rt_surface_debug) {
		RENDER_TIMESTAMP("Yutrel RT Surface Debug View");
		if (!rt_ready || !debug_view_pass.render_rt_surface(targets, frame_buffer, rt_scene, settings.debug_view)) {
			const YutrelScopedLabel clear_label("Yutrel Clear Output", Color(0.3, 0.3, 0.4));
			rd->draw_list_begin(targets.output_framebuffer, RD::DRAW_CLEAR_COLOR_0, { Color(0, 0, 0, 1) }, 0.0f, 0, targets.output_region);
			rd->draw_list_end();
		}
	} else {
		RENDER_TIMESTAMP("Yutrel Debug View");
		debug_view_pass.render(targets, settings.debug_view, primary_shadow);
	}

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint()) {
		RENDER_TIMESTAMP("Yutrel Editor Gizmos");
		editor_gizmos.render(targets, *p_render_data->scene_data, frame);
	}
#endif
	custom->has_prev_camera = true;
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
	const bool enabled = GLOBAL_DEF("rendering/yutrel/path_tracer/enabled", false);
	runtime_path_tracing = enabled && !Engine::get_singleton()->is_editor_hint();
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
