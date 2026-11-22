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
	float4 data = probe_data[coord];
	if (volume.classification_enabled == 0 || !ddgi_uses_fixed_rays(volume))
	{
		probe_data[coord] = float4(data.xyz, 0);
		return;
	}

	float distances[DDGI_NUM_FIXED_RAYS];
	uint backfaces = 0;
	for (uint ray = 0; ray < DDGI_NUM_FIXED_RAYS; ++ray)
	{
		float dist = ray_data.Load(int4(ray, coord.y * counts.x + coord.x, coord.z, 0)).a;
		distances[ray] = dist;
		if (dist < 0)
			++backfaces;
	}
	if (float(backfaces) / DDGI_NUM_FIXED_RAYS > volume.fixed_ray_backface_threshold)
	{
		probe_data[coord] = float4(data.xyz, 1);
		return;
	}

	for (uint ray = 0; ray < DDGI_NUM_FIXED_RAYS; ++ray)
	{
		if (distances[ray] < 0)
			continue;
		float3 dir = abs(ddgi_ray_direction(ray, volume));
		float3 plane_dist = select(dir > 1e-6, volume.spacing / dir, float3(1e27));
		float max_dist = min(plane_dist.x, min(plane_dist.y, plane_dist.z));
		if (distances[ray] <= max_dist)
		{
			probe_data[coord] = float4(data.xyz, 0);
			return;
		}
	}
	probe_data[coord] = float4(data.xyz, 1);
}
