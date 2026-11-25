import common;
import temporal_reprojection;
import rt_shadow_common;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(2, 0)]] Texture2D<uint2> curr_surface;
[[vk::binding(3, 0)]] Texture2D<uint2> prev_surface;
[[vk::binding(4, 0)]] Texture2D<float2> raw_shadow;
[[vk::binding(5, 0)]] Texture2D<float4> prev_history;
[[vk::binding(6, 0)]] Texture2D<float2> work_velocity;
[[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> curr_history;

struct TemporalParams
{
	ShadowGrid grid;
	uint has_history;
	float max_history, dt_scale;
};
[[vk::push_constant]] ConstantBuffer<TemporalParams> params;

groupshared float4 tile_nd[10][10];
groupshared float2 tile_raw[10][10];

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
	uint width, height;
	raw_shadow.GetDimensions(width, height);
	int2 size = int2(width, height);
	for (uint idx = lane; idx < 100; idx += 64)
	{
		int2 q = int2(idx % 10, idx / 10);
		int2 tap = int2(group.xy) * 8 + q - 1;
		if (all(tap >= 0) && all(tap < size))
		{
			tile_nd[q.y][q.x] = load_surface(curr_surface, tap);
			tile_raw[q.y][q.x] = raw_shadow.Load(int3(tap, 0));
		}
		else
			tile_nd[q.y][q.x] = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = params.grid.frame_pixel(int2(tid.xy), frame);
	int2 center = int2(local.xy) + 1;
	float4 nd = tile_nd[center.y][center.x];
	float depth = nd.w;
	float2 raw = tile_raw[center.y][center.x];
	float4 result = float4(raw, 1, 0);
	if (depth > 0)
	{
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 N = nd.xyz;
		float threshold = max(0.02, 0.02 * length(frame.camera_position.xyz - world));
		float4 curr_plane_c = surface_plane_coeffs(world, N, frame);
		float sum = raw.x, sum2 = raw.x * raw.x, taps = 1;
		for (int y = -1; y <= 1; ++y)
			for (int x = -1; x <= 1; ++x)
			{
				int2 tap = int2(tid.xy) + int2(x, y);
				int2 q = center + int2(x, y);
				float4 tap_nd = tile_nd[q.y][q.x];
				if ((x == 0 && y == 0) || tap_nd.w <= 0 || dot(N, tap_nd.xyz) <= 0.75)
					continue;
				int2 p = params.grid.frame_pixel(tap, frame);
				if (!plane_compatible(curr_plane_c, frame.inv_view_projection[3], surface_clip(p, tap_nd.w, frame), threshold))
					continue;
				float2 v = tile_raw[q.y][q.x];
				result.g = max(result.g, v.g);
				sum += v.x;
				sum2 += v.x * v.x;
				taps++;
			}
		float weight_sum = 0;
		float3 history_sum = 0;
		if (params.has_history != 0)
		{
			float2 vel = work_velocity.Load(int3(params.grid.pixel_scale == 1 ? pixel : int2(tid.xy), 0));
			SurfaceReprojection reproj = reproject_surface(pixel, vel, world, frame, prev_frame);
			history_sum = gather_surface_history(prev_surface, prev_history, reproj, world, N, threshold,
					frame, prev_frame, params.grid.pixel_scale, params.grid.full_size, size, weight_sum).rgb;
		}
		if (weight_sum > 1e-4)
		{
			float3 history = history_sum / weight_sum;
			float count = min(history.z + 1, params.max_history);
			float alpha = 1 - pow(1 - rcp(params.max_history), params.dt_scale);
			if (count < params.max_history)
				alpha = max(alpha, rcp(count));
			float clamp_weight = saturate((taps - 4.0) / 5.0) * 0.25;
			if (count >= 4 && clamp_weight > 0.0)
			{
				float mean = sum / taps;
				float extent = 2 * sqrt(max(0, sum2 / taps - mean * mean));
				history.x = lerp(history.x, clamp(history.x, mean - extent, mean + extent), clamp_weight);
			}
			result = float4(lerp(history.xy, result.rg, alpha), count, 0);
		}
	}
	curr_history[tid.xy] = result;
}
