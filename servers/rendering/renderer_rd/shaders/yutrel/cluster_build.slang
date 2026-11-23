import local_lights;

[[vk::binding(0, 0)]] ConstantBuffer<ClusterParams> cp;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint2> grid;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> indices;

float slice_depth(uint slice)
{
	return cp.is_ortho != 0 ? cp.z_near + slice / cp.slice_scale : cp.z_near * exp(slice / cp.slice_scale);
}

bool sphere_intersects(float3 center, float radius, float3 lower, float3 upper)
{
	float3 delta = center - clamp(center, lower, upper);
	return dot(delta, delta) <= radius * radius;
}

[numthreads(8, 4, 2)]
[shader("compute")]
void compute_main(uint3 cell : SV_DispatchThreadID)
{
	if (any(cell >= cp.grid_size))
		return;
	float near_z = slice_depth(cell.z);
	float far_z = slice_depth(cell.z + 1);
	float3 lower = float3(1e30);
	float3 upper = -lower;
	for (uint i = 0; i < 4; ++i)
	{
		float2 ndc = (cell.xy + uint2(i & 1, i >> 1)) * (2.0 / cp.grid_size.xy) - 1;
		float4 h = mul(cp.inv_projection, float4(ndc, 1, 1));
		float3 corner = h.xyz / h.w;
		float2 slope = cp.is_ortho != 0 ? float2(0) : corner.xy / -corner.z;
		float3 a = float3(corner.xy + slope * (near_z + corner.z), -near_z);
		float3 b = float3(corner.xy + slope * (far_z + corner.z), -far_z);
		lower = min(lower, min(a, b));
		upper = max(upper, max(a, b));
	}
	float3 center = 0.5 * (lower + upper);
	float radius = length(upper - center);
	uint id = cluster_id(cell, cp.grid_size);
	uint base = id * MAX_LIGHTS_PER_CLUSTER;
	uint count = 0;
	for (uint i = 0; i < cp.light_count && count < MAX_LIGHTS_PER_CLUSTER; ++i)
	{
		LocalLightData light = local_lights[i];
		float3 pos = mul(cp.view_matrix, float4(light.position, 1)).xyz;
		if (!sphere_intersects(pos, light.range, lower, upper))
			continue;
		if ((light.flags & IS_SPOT) != 0)
		{
			float3 axis = mul(cp.view_matrix, float4(light.direction, 0)).xyz;
			float c = light.spot_cos_outer;
			float sn = sqrt(saturate(1 - c * c));
			// Minimal enclosing sphere of the range-limited spherical sector.
			float offset = c >= 0.70710678 ? light.range / (2 * c) : light.range * c;
			float bound = c >= 0.70710678 ? offset : light.range * sn;
			if (!sphere_intersects(pos + axis * offset, bound, lower, upper))
				continue;
			float3 delta = center - pos;
			float axial = dot(delta, axis);
			if (axial < -radius || length(delta - axial * axis) * c - axial * sn > radius)
				continue;
		}
		indices[base + count++] = i;
	}
	grid[id] = uint2(base, count);
}
