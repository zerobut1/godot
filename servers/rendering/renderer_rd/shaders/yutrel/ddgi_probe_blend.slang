import ddgi_common;

[[vk::binding(0, 0)]] public ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(1, 0)]] Texture2DArray<float4> ray_data;

[[vk::binding(3, 0)]] Texture2DArray<float4> probe_data;

public bool blend_probe_is_active(uint3 group)
{
	return ddgi_probe_is_active(int3(group.xzy), volume, probe_data);
}

public uint blend_ray_count()
{
	return max(volume.rays_per_probe - ddgi_first_blend_ray(volume), 1u);
}

static const uint CHUNK = 256;
groupshared float4 samples[CHUNK];
groupshared float3 directions[CHUNK];

public bool is_border(uint2 pixel, uint tile)
{
	return any(pixel == 0) || any(pixel == tile - 1);
}

// Octahedral edges fold onto a mirrored interior row/column; corners wrap both axes.
public uint2 border_source(uint2 pixel, uint tile)
{
	bool2 edge = (pixel == 0) | (pixel == tile - 1);
	uint2 mirrored = tile - 1 - pixel;
	uint2 inner = clamp(pixel, 1u, tile - 2);
	if (all(edge))
		return tile - 1 - inner;
	return edge.x ? uint2(inner.x, mirrored.y) : uint2(mirrored.x, inner.y);
}

public float3 integrate_rays(uint3 group, uint2 pixel, uint lane, uint tile, bool blend_distance, out uint backfaces)
{
	float3 sum = 0;
	float weight_sum = 0;
	backfaces = 0;
	float3 oct_dir = ddgi_oct_decode((float2(pixel) - 0.5) * (2.0 / (tile - 2)) - 1);
	uint row = group.y * volume.probe_counts.x + group.x;
	// Keep both half-distance moments representable even in very large volumes.
	float max_distance = min(length(volume.spacing) * 1.5, sqrt(2.0 * 65504.0));
	uint first_ray = ddgi_first_blend_ray(volume);
	for (uint first = first_ray; first < volume.rays_per_probe; first += CHUNK)
	{
		uint count = min(CHUNK, volume.rays_per_probe - first);
		for (uint j = lane; j < count; j += tile * tile)
		{
			samples[j] = ray_data.Load(int4(first + j, row, group.z, 0));
			directions[j] = ddgi_ray_direction(first + j, volume);
		}
		GroupMemoryBarrierWithGroupSync();
		if (!is_border(pixel, tile))
		{
			for (uint i = 0; i < count; ++i)
			{
				float4 ray = samples[i];
				float weight = max(0.0, dot(oct_dir, directions[i]));
				if (blend_distance)
				{
					float d = min(abs(ray.a), max_distance);
					weight = pow(weight, volume.distance_exponent);
					sum.xy += weight * float2(d, d * d);
				}
				else if (ray.a < 0)
				{
					++backfaces;
					continue;
				}
				else
					sum += weight * ray.rgb;
				weight_sum += weight;
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}
	return sum / (2 * max(weight_sum, blend_ray_count() * 1e-9));
}
