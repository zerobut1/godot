import ddgi_common;

[[vk::binding(0, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(1, 0)]] Texture2DArray<float4> ray_data;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> probe_data;

[shader("compute")]
[numthreads(32, 1, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	int3 counts = volume.probe_counts;
	if (tid.x >= uint(counts.x * counts.y * counts.z))
		return;
	int3 coord = ddgi_probe_data_coord(ddgi_probe_grid(tid.x, volume));
	float4 prev = volume.reset_history != 0 ? float4(0) : probe_data[coord];
	if (volume.relocation_enabled == 0 || !ddgi_uses_fixed_rays(volume))
	{
		probe_data[coord] = float4(0, 0, 0, prev.w);
		return;
	}

	float3 offset = prev.xyz * volume.spacing;
	uint backfaces = 0;
	int closest_back = -1, closest_front = -1, farthest_front = -1;
	float closest_back_dist = 1e27, closest_front_dist = 1e27, farthest_front_dist = -1;
	for (uint ray = 0; ray < DDGI_NUM_FIXED_RAYS; ++ray)
	{
		float dist = ray_data.Load(int4(ray, coord.y * counts.x + coord.x, coord.z, 0)).a;
		if (dist < 0)
		{
			++backfaces;
			dist *= -5.0;
			if (dist < closest_back_dist)
			{
				closest_back_dist = dist;
				closest_back = ray;
			}
		}
		else
		{
			if (dist < closest_front_dist)
			{
				closest_front_dist = dist;
				closest_front = ray;
			}
			if (dist > farthest_front_dist)
			{
				farthest_front_dist = dist;
				farthest_front = ray;
			}
		}
	}

	float3 full_offset = offset;
	if (closest_back != -1 && float(backfaces) / DDGI_NUM_FIXED_RAYS > volume.fixed_ray_backface_threshold)
	{
		full_offset += ddgi_ray_direction(closest_back, volume) * (closest_back_dist + 0.5 * volume.min_frontface_distance);
	}
	else if (closest_front_dist < volume.min_frontface_distance && closest_front != -1 && farthest_front != -1)
	{
		float3 closest_dir = ddgi_ray_direction(closest_front, volume);
		float3 farthest_dir = ddgi_ray_direction(farthest_front, volume);
		if (dot(closest_dir, farthest_dir) <= 0)
		{
			float push = min(0.5 * farthest_front_dist, min(1.0, volume.min_frontface_distance - closest_front_dist));
			full_offset += farthest_dir * push;
		}
	}
	else if (closest_front_dist > volume.min_frontface_distance)
	{
		float offset_len = length(offset);
		if (offset_len > 1e-5)
		{
			float margin = min(closest_front_dist - volume.min_frontface_distance, offset_len);
			full_offset -= (offset / offset_len) * margin;
		}
		else
			full_offset = 0;
	}

	float3 norm_offset = full_offset / volume.spacing;
	if (dot(norm_offset, norm_offset) < 0.2025)
		offset = full_offset;
	probe_data[coord] = float4(offset / volume.spacing, prev.w);
}
