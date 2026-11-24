import common;
import temporal_reprojection;
import rt_reflection_common;

[[vk::binding(0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(2)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3)]] Texture2D<float> scene_depth;
[[vk::binding(4)]] Texture2D<float4> raw_reflection;
[[vk::binding(5)]] Texture2D<float4> prev_history;
[[vk::binding(6)]] Texture2D<float4> prev_guide;
[[vk::binding(7)]] Texture2D<float2> prev_stats;
[[vk::binding(8)]] Texture2D<float4> gbuffer_c;
[[vk::binding(9)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> guide;
[[vk::binding(10)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> reprojected;
[[vk::binding(11)]] [[vk::image_format("r16f")]] RWTexture2D<float> variance;
[[vk::binding(12)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> average;
[[vk::binding(13)]] [[vk::image_format("r8ui")]] Texture2D<uint> quad_offset;

[[vk::binding(14)]] Texture2D<float2> velocity;

struct ReprojectParams
{
	int2 full_size;
	uint scale, has_history, stationary;
};
[[vk::push_constant]] ConstantBuffer<ReprojectParams> params;

float2 history_texel(float3 world)
{
	return reflection_texel(previous_pixel(world, prev_frame), params.scale, float2(0.0), params.full_size);
}

float4 nearest_guide(float2 p, int2 size)
{
	int2 tap = int2(floor(p + 0.5));
	return all(tap >= 0) && all(tap < size) ? prev_guide.Load(int3(tap, 0)) : float4(0);
}

void recover_history(int2 texel, int2 pixel, int2 size, float3 world, float distance, float3 N, float r, float3 current,
		out float4 history, out float var)
{
	history = float4(0, 0, 0, 1);
	var = 1;
	// Reuse the exact stationary sample to avoid diffusing a sharp lobe.
	float2 p = float2(texel);
	float2 vel = velocity.Load(int3(pixel, 0));
	SurfaceReprojection reproj = reproject_surface(pixel, vel, world, frame, prev_frame);
	if (params.stationary == 0 || any(abs(vel) >= 1e-5))
	{
		if (!reproj.visible)
			return;
		float2 prev_pixel = reproj.prev_uv / prev_frame.inv_viewport_size - 0.5;
		float2 surface = reflection_texel(prev_pixel, params.scale, float2(0), params.full_size);
		float2 hit = history_texel(world - reflection_view(world, frame) * distance);
		p = !reproj.moving && params.stationary == 0 && use_virtual_hit(N, r, nearest_guide(hit, size), nearest_guide(surface, size)) ? hit : surface;
	}
	if (any(p < -0.5) || any(p > size - 0.5))
		return;

	int2 base = int2(floor(p));
	float2 f = frac(p);
	float z = linear_view_depth(world, prev_frame);
	float weight = 0;
	float3 color = 0;
	float2 stats = 0;
	// Reconstruct the bilinear footprint from surviving reflector samples only.
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 tap = clamp(base + int2(x, y), 0, size - 1);
			int3 tc = int3(tap, 0);
			float4 g = prev_guide.Load(tc);
			if (g.z <= 0 || g.w < MIRROR_ROUGHNESS || abs(g.w - r) > 0.1)
				continue;
			float confidence = reproj.moving ? float(moving_surface_compatible(N, linear_view_depth(world, frame), unpack_octahedral_normal(g.xy), g.z))
					: disocclusion(N, z, g);
			if (confidence < 0.9)
				continue;
			float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			color += w * prev_history.Load(tc).rgb;
			stats += w * prev_stats.Load(tc) * float2(1, confidence);
			weight += w;
		}
	if (weight > 1e-3)
	{
		color /= weight;
		stats /= weight;
		float count = min(stats.y + 1, max(8.0, 32 * (1 - exp(-r * 100))));
		history = float4(color, count);
		var = lerp(temporal_variance(color, current), stats.x, rcp(count));
	}
}

groupshared float4 tile_sum[64];

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint lane : SV_GroupIndex, uint3 group : SV_GroupID)
{
	int2 size = (params.full_size + int(params.scale) - 1) / int(params.scale);
	float4 weighted = 0;
	if (all(int2(tid.xy) < size))
	{
		int2 p = reflection_sample_pixel(tid.xy, params.scale, quad_offset.Load(int3(tid.xy, 0)), params.full_size) + int2(frame.viewport_origin);
		int3 coord = int3(p, 0);
		float4 raw = raw_reflection.Load(int3(tid.xy, 0));
		float4 g = 0;
		if (raw.a >= 0)
		{
			float3 world = reconstruct_world_position(p + 0.5, scene_depth.Load(coord), frame);
			float3 N = reflection_normal(gbuffer_b.Load(coord));
			float r = gbuffer_c.Load(coord).r;
			g = reflection_guide(N, linear_view_depth(world, frame), r);
			if (r >= MIRROR_ROUGHNESS)
			{
				float4 history = float4(0, 0, 0, 1);
				float var = 1;
				if (params.has_history != 0)
					recover_history(tid.xy, p, size, world, raw.a, N, r, raw.rgb, history, var);
				reprojected[tid.xy] = history;
				variance[tid.xy] = var;
				float3 c = history.a > 1 ? lerp(raw.rgb, history.rgb, 0.3) : raw.rgb;
				weighted = float4(c, 1) * max(exp(-0.3 * reflection_luma(c)), 0.01);
			}
		}
		guide[tid.xy] = g;
	}
	tile_sum[lane] = weighted;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (lane < stride)
			tile_sum[lane] += tile_sum[lane + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0)
		average[group.xy] = float4(tile_sum[0].rgb / max(tile_sum[0].a, 1e-3), tile_sum[0].a > 0 ? 1 : 0);
}
