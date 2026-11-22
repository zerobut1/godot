import common;
import brdf;
import ibl_common;
import rt_scene;
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
// Binding 8 is reserved for the Step 2B ProbeData array.
struct TraceParams
{
	uint light_count, has_tlas;
	float sky_intensity, sky_sin_rot, sky_cos_rot;
};
[[vk::push_constant]] ConstantBuffer<TraceParams> params;

[shader("compute")]
[numthreads(64, 1, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x >= volume.rays_per_probe)
		return;
	int3 grid = int3(tid.y % volume.probe_counts.x, tid.z, tid.y / volume.probe_counts.x);
	float3 dir = ddgi_ray_direction(tid.x, volume);
	RTHit hit;
	float3 radiance;
	float hit_t;
	if (params.has_tlas == 0 || !trace_surface(ddgi_probe_world_position(grid, volume), dir, 0, volume.max_ray_distance, hit, true))
	{
		float3 sky_dir = inverse_sky_rotation(dir, params.sky_sin_rot, params.sky_cos_rot);
		radiance = sky_cubemap.SampleLevel(cube_sampler, sky_dir, 0).rgb * params.sky_intensity;
		// The reference miss sentinel becomes +infinity in FP16. Distance blending clamps it before squaring.
		hit_t = 1e27;
	}
	else if (!hit.is_front_face && !hit.is_double_sided)
	{
		radiance = 0;
		hit_t = -0.2 * hit.t;
	}
	else
	{
		hit_t = hit.t;
		radiance = hit.emissive;
		if (hit.is_unlit)
			radiance += hit.base_color;
		else
		{
			float3 albedo = hit.base_color * (1 - hit.metallic);
			float3 origin = offset_ray_origin(hit.world_pos, hit.geometric_normal);
			for (uint i = 0; i < params.light_count; ++i)
			{
				float3 L = lights[i].direction.xyz;
				float NoL = dot(hit.shading_normal, L);
				if (NoL > 0 && !trace_shadow_ray(origin, L, 0, 1e20))
					radiance += albedo * (NoL / PI) * lights[i].radiance;
			}
			if (volume.infinite_bounces != 0 && volume.reset_history == 0)
			{
				float weight = ddgi_volume_blend_weight(hit.world_pos, volume);
				if (weight > 0)
				{
					float3 bias = ddgi_surface_bias(hit.shading_normal, dir, volume);
					DDGIResources probes = { irradiance, distance, linear_clamp };
					float3 E = ddgi_get_volume_irradiance(hit.world_pos, bias, hit.shading_normal, volume, probes);
					radiance += min(albedo, 0.9) * (weight / PI) * E;
				}
			}
		}
	}
	ray_data[tid] = float4(clamp(sanitize_finite(radiance), 0, 65504), hit_t);
}
