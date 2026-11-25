import rt_reflection_common;

[[vk::binding(7)]] SamplerState linear_clamp;
[[vk::binding(0)]] Texture2D<float4> raw_reflection;
[[vk::binding(1)]] Texture2D<float4> guide;
[[vk::binding(2)]] Texture2D<float4> filtered;
[[vk::binding(3)]] Texture2D<float4> reprojected;
[[vk::binding(4)]] Texture2D<float4> average;
[[vk::binding(5)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> history;
[[vk::binding(6)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> stats;

struct ResolveParams
{
	int2 size;
	float dt_scale;
};
[[vk::push_constant]] ConstantBuffer<ResolveParams> params;

groupshared float4 neighborhood[16][16];
static const float G[5] = { 1.0, 0.886920437, 0.618783392, 0.339595526, 0.146606962 };

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint3 local : SV_GroupThreadID, uint3 group : SV_GroupID)
{
	float4 avg = average.Load(int3(group.xy, 0));
	if (avg.a > 0)
	{
		for (int y = 0; y < 16; y += 8)
			for (int x = 0; x < 16; x += 8)
			{
				int2 q = int2(local.xy) + int2(x, y);
				int3 tc = int3(clamp(int2(group.xy) * 8 + q - 4, 0, params.size - 1), 0);
				float4 g = guide.Load(tc);
				neighborhood[q.y][q.x] = g.z > 0 && g.w >= MIRROR_ROUGHNESS ? float4(filtered.Load(tc).rgb, 1) : float4(0);
			}
		GroupMemoryBarrierWithGroupSync();
	}
	if (any(int2(tid.xy) >= params.size))
		return;
	int3 tc = int3(tid.xy, 0);
	float4 g = guide.Load(tc);
	if (g.z <= 0 || g.w < MIRROR_ROUGHNESS)
	{
		history[tid.xy] = g.z > 0 ? float4(raw_reflection.Load(tc).rgb, 1) : float4(0);
		return;
	}

	float3 sum = 0, sum_sq = 0;
	float weights = 0;
	// Separable Gaussian weights exp(-3*i*i/25) over the FidelityFX 9x9 support.
	for (int y = -4; y <= 4; ++y)
	{
		float wy = G[abs(y)];
		int row = int(local.y) + 4 + y;
		for (int x = -4; x <= 4; ++x)
		{
			float4 tap = neighborhood[row][int(local.x) + 4 + x];
			float w = tap.a * wy * G[abs(x)];
			sum += tap.rgb * w;
			sum_sq += tap.rgb * tap.rgb * w;
			weights += w;
		}
	}
	float3 tile_average = sample_tile_average(average, linear_clamp, tid.xy, params.size);
	float3 mean = sum / weights;
	float3 extent = 1.4 * (sqrt(abs(sum_sq / weights - mean * mean)) + length(mean - tile_average));
	float4 prior = reprojected.Load(tc);
	float4 sample = filtered.Load(tc);
	float3 old = clip_radiance(lerp(mean, tile_average, 0.2), extent, prior.rgb);
	float3 current = clip_radiance(tile_average, extent, lerp(sample.rgb, tile_average, rcp(prior.a + 1)));
	float weight = min(1 - rcp(prior.a), pow(1 - 1.0 / 32, params.dt_scale));
	float3 result = lerp(current, old, weight);
	history[tid.xy] = float4(clamp(result, 0, 65504), 1);
	stats[tid.xy] = float2(lerp(temporal_variance(result, old), sample.a, weight), prior.a);
}
