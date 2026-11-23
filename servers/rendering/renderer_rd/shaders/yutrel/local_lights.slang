import pbr;

public struct LocalLightData
{
	public float3 position;
	public float inv_range;
	public float3 radiance;
	public float attenuation;
	public float3 direction;
	public float spot_cos_outer;
	public float spot_attenuation, shadow_opacity, range;
	public uint flags;
};

public static const uint IS_SPOT = 1;
public static const uint HAS_SHADOW = 2;
public static const uint MAX_LIGHTS_PER_CLUSTER = 128;

public struct ClusterParams
{
	public row_major float4x4 view_matrix, inv_projection;
	public uint3 grid_size;
	public uint light_count;
	public float z_near, z_far, slice_scale;
	public uint is_ortho;
	public float2 viewport_origin, inv_viewport_size;
};

public float evaluate_local_light_attenuation(LocalLightData light, float3 world, out float3 L, out float dist)
{
	float3 to_light = light.position - world;
	dist = length(to_light);
	L = 0;
	if (dist >= light.range || dist <= 1e-5)
		return 0;
	L = to_light / dist;
	float nd = dist * light.inv_range;
	nd *= nd;
	nd *= nd;
	nd = max(1.0 - nd, 0.0);
	float atten = nd * nd * pow(max(dist, 0.0001), -light.attenuation);
	if ((light.flags & IS_SPOT) != 0)
	{
		float scos = dot(-L, light.direction);
		if (scos <= light.spot_cos_outer)
			return 0;
		float rim = max(1e-4, (1.0 - scos) / max(1.0 - light.spot_cos_outer, 1e-4));
		atten *= saturate(1.0 - pow(rim, light.spot_attenuation));
	}
	return atten;
}

public uint cluster_id(uint3 cell, uint3 size)
{
	return (cell.z * size.y + cell.y) * size.x + cell.x;
}

public uint cluster_index_from_screen(float2 pixel_xy, float linear_z, ClusterParams cp)
{
	float2 uv = saturate((pixel_xy - cp.viewport_origin) * cp.inv_viewport_size);
	float z = cp.is_ortho != 0 ? (linear_z - cp.z_near) * cp.slice_scale :
			log(max(linear_z, cp.z_near) / cp.z_near) * cp.slice_scale;
	return cluster_id(min(uint3(float3(uv * cp.grid_size.xy, max(z, 0.0))), cp.grid_size - 1), cp.grid_size);
}

public interface ILocalLightShadows
{
	float local_visibility(LocalLightData light, float3 L, float dist);
}

public struct UnshadowedLocalLights : ILocalLightShadows
{
	public float local_visibility(LocalLightData light, float3 L, float dist) { return 1; }
};

[[vk::binding(16, 0)]] public StructuredBuffer<LocalLightData> local_lights;
[[vk::binding(17, 0)]] StructuredBuffer<uint2> cluster_grid;
[[vk::binding(18, 0)]] StructuredBuffer<uint> cluster_indices;
[[vk::binding(19, 0)]] ConstantBuffer<ClusterParams> clusters;

public float3 shade_cluster<S : ILocalLightShadows>(float2 pixel, float3 world, ShadingData p, S shadows)
{
	if (clusters.light_count == 0)
		return 0;
	float z = -mul(clusters.view_matrix, float4(world, 1)).z;
	uint2 list = cluster_grid[cluster_index_from_screen(pixel, z, clusters)];
	float3 sum = 0;
	for (uint i = 0; i < list.y; ++i)
	{
		LocalLightData light = local_lights[cluster_indices[list.x + i]];
		float3 L;
		float dist;
		float atten = evaluate_local_light_attenuation(light, world, L, dist);
		float NoL = saturate(dot(p.N, L));
		if (atten > 0 && NoL > 0)
			sum += direct_brdf(p, L, NoL) * light.radiance * (NoL * atten * shadows.local_visibility(light, L, dist));
	}
	return sum;
}
