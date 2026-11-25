import common;
import brdf;
import ibl_common;
import rt_scene;
import local_lights;
import local_light_ray;
import ddgi_common;

struct DirectionalLight
{
	float4 direction;
	float3 radiance;
	uint has_shadow;
};

[[vk::binding(0, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(1, 0)]] StructuredBuffer<DirectionalLight> lights;
[[vk::binding(2, 0)]] TextureCube<float4> sky_cubemap;
[[vk::binding(3, 0)]] SamplerState cube_sampler;
[[vk::binding(4, 0)]] Texture2DArray<float4> irradiance;
[[vk::binding(5, 0)]] Texture2DArray<float2> distance;
[[vk::binding(6, 0)]] SamplerState linear_clamp;
[[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> ray_data;
[[vk::binding(8, 0)]] Texture2DArray<float4> probe_data;

struct TraceParams
{
	uint light_count, has_tlas;
	float sky_intensity, sky_sin_rot, sky_cos_rot;
	uint local_count;
};
[[vk::push_constant]] ConstantBuffer<TraceParams> params;

[shader("compute")]
[numthreads(64, 1, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x >= volume.rays_per_probe)
		return;
	int3 grid = int3(tid.y % volume.probe_counts.x, tid.z, tid.y / volume.probe_counts.x);
	bool uses_fixed = ddgi_uses_fixed_rays(volume);
	bool is_fixed_ray = uses_fixed && tid.x < DDGI_NUM_FIXED_RAYS;
	bool fixed_ready = volume.reset_history == 0 && uses_fixed;
	bool use_classification = fixed_ready && volume.classification_enabled != 0;
	bool use_relocation = fixed_ready && volume.relocation_enabled != 0;
	float4 pd = (use_classification || use_relocation) ? probe_data.Load(int4(ddgi_probe_data_coord(grid), 0)) : float4(0);
	bool active = !use_classification || pd.w < 0.5;
	if (!active && !is_fixed_ray)
		return;

	float3 dir = ddgi_ray_direction(tid.x, volume);
	float3 probe_position = ddgi_probe_base_position(grid, volume) + (use_relocation ? pd.xyz * volume.spacing : float3(0));
	RTHit hit = {};
	bool found = false;
	if (params.has_tlas != 0)
		found = trace_surface(probe_position, dir, 0, volume.max_ray_distance, hit, true, is_fixed_ray);
	// The miss sentinel becomes +infinity in FP16. Distance blending clamps it before squaring.
	float hit_t = 1e27;
	if (found)
		hit_t = !hit.is_front_face && !hit.is_double_sided ? -0.2 * hit.t : hit.t;
	if (is_fixed_ray)
	{
		ray_data[tid] = float4(0, 0, 0, hit_t);
		return;
	}

	float3 radiance;
	if (!found)
	{
		float3 sky_dir = inverse_sky_rotation(dir, params.sky_sin_rot, params.sky_cos_rot);
		radiance = sky_cubemap.SampleLevel(cube_sampler, sky_dir, 0).rgb * params.sky_intensity;
	}
	else if (!hit.is_front_face && !hit.is_double_sided)
	{
		radiance = 0;
	}
	else
	{
		radiance = hit.surface.self_illumination();
		if (hit.surface.shading_model_id == SHADING_STANDARD)
		{
			float3 albedo = hit.surface.base_color * (1 - hit.surface.metallic);
			float3 origin = offset_ray_origin(hit.world_pos, hit.geometric_normal);
			for (uint i = 0; i < params.light_count; ++i)
			{
				float3 L = lights[i].direction.xyz;
				float NoL = dot(hit.surface.normal_ws, L);
				if (NoL > 0 && !trace_shadow_ray(origin, L, 0, 1e20))
					radiance += albedo * (NoL / PI) * lights[i].radiance;
			}
			LocalLightRay shadow = { origin, hit.geometric_normal, 0 };
			for (uint i = 0; i < params.local_count; ++i)
			{
				float3 L;
				float NoL;
				radiance += (albedo / PI) * shadow.incident_light(local_lights[i], hit.world_pos, hit.surface.normal_ws, L, NoL);
			}
			if (volume.infinite_bounces != 0 && volume.reset_history == 0)
			{
				float weight = ddgi_volume_blend_weight(hit.world_pos, volume);
				if (weight > 0)
				{
					float3 bias = ddgi_surface_bias(hit.surface.normal_ws, dir, volume);
					DDGIResources probes = { irradiance, distance, probe_data, linear_clamp };
					float3 E = ddgi_get_volume_irradiance(hit.world_pos, bias, hit.surface.normal_ws, volume, probes);
					radiance += min(albedo, 0.9) * (weight / PI) * E;
				}
			}
		}
	}
	ray_data[tid] = float4(clamp(sanitize_finite(radiance), 0, 65504), hit_t);
}
