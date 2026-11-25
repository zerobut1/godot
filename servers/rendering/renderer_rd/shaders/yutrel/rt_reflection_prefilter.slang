import common;
import rt_reflection_common;

[[vk::binding(5)]] SamplerState linear_clamp;
[[vk::binding(0)]] Texture2D<float4> raw_reflection;
[[vk::binding(1)]] Texture2D<float4> guide;
[[vk::binding(2)]] Texture2D<float> variance;
[[vk::binding(3)]] Texture2D<float4> average;
[[vk::binding(4)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> filtered;
[[vk::push_constant]] ConstantBuffer<int2> work_size;

groupshared float4 tile_radiance[16][16]; // Variance in alpha, negative for non-glossy taps.
groupshared float4 tile_surface[16][16]; // Decoded normal and linear depth.
groupshared float tile_roughness[16][16];

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint3 local : SV_GroupThreadID, uint3 group : SV_GroupID)
{
	float4 avg = average.Load(int3(group.xy, 0));
	// Both filter stages bypass mirror-only and empty tiles uniformly before the barrier.
	if (avg.a == 0)
		return;
	int2 size = work_size;
	for (int y = 0; y < 16; y += 8)
		for (int x = 0; x < 16; x += 8)
		{
			int2 q = int2(local.xy) + int2(x, y);
			int3 tc = int3(clamp(int2(group.xy) * 8 + q - 4, 0, size - 1), 0);
			float4 g = guide.Load(tc);
			float var = g.z > 0 && g.w >= MIRROR_ROUGHNESS ? variance.Load(tc) : -1;
			tile_surface[q.y][q.x] = float4(unpack_octahedral_normal(g.xy), g.z);
			tile_roughness[q.y][q.x] = g.w;
			tile_radiance[q.y][q.x] = float4(raw_reflection.Load(tc).rgb, var);
		}
	GroupMemoryBarrierWithGroupSync();
	int2 q = int2(local.xy) + 4;
	float4 c = tile_radiance[q.y][q.x];
	if (any(int2(tid.xy) >= size) || c.a < 0)
		return;
	float4 surface = tile_surface[q.y][q.x];
	float r = tile_roughness[q.y][q.x];
	float3 tile_average = sample_tile_average(average, linear_clamp, tid.xy, size);
	float normal_power = normal_sigma(r);
	float variance_weight = max(0.1, 1 - exp(-4.4 * c.a));
	float inv_depth_scale = -4.0 / max(0.05, 0.04 * surface.w);
	float rad_scale = -(0.6 + 0.1 * c.a);
	float sum_w = max(exp(rad_scale * length(tile_average - c.rgb)), 0.01);
	float3 sum = sum_w * c.rgb;
	float var_sum = sum_w * sum_w * c.a;
	static const int2 offsets[15] = {
		int2(0,1), int2(-2,1), int2(2,-3), int2(-3,0), int2(1,2), int2(-1,-2), int2(3,0), int2(-3,3),
		int2(0,-3), int2(-1,-1), int2(2,1), int2(-2,-2), int2(1,0), int2(0,2), int2(3,-1)
	};
	for (int i = 0; i < 15; ++i)
	{
		int2 t = q + offsets[i];
		float4 tap = tile_radiance[t.y][t.x];
		if (tap.a < 0)
			continue;
		float4 g = tile_surface[t.y][t.x];
		float w = pow(saturate(dot(surface.xyz, g.xyz)), normal_power);
		float depth_rough_term = inv_depth_scale * abs(surface.w - g.w) - 32 * abs(r - tile_roughness[t.y][t.x]);
		float rad_term = max(rad_scale * length(tile_average - tap.rgb), -4.605170186);
		w *= variance_weight * exp(depth_rough_term + rad_term);
		sum += w * tap.rgb;
		sum_w += w;
		var_sum += w * w * tap.a;
	}
	filtered[tid.xy] = float4(sum / sum_w, var_sum / (sum_w * sum_w));
}
