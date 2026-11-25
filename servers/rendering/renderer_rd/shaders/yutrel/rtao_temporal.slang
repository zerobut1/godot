import common;
import temporal_reprojection;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(2, 0)]] Texture2D<uint2> curr_surface;
[[vk::binding(3, 0)]] Texture2D<uint2> prev_surface;
[[vk::binding(4, 0)]] Texture2D<float> raw_ao;
[[vk::binding(5, 0)]] Texture2D<float4> prev_history;
[[vk::binding(6, 0)]] Texture2D<float2> work_velocity;
[[vk::binding(7, 0)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> curr_history;

struct TemporalParams
{
	int2 full_size;
	uint pixel_scale;
	uint has_history;
	float max_history, dt_scale;
};
[[vk::push_constant]] ConstantBuffer<TemporalParams> params;

groupshared float4 tile_nd[10][10];
groupshared float tile_raw[10][10];

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint lane : SV_GroupIndex)
{
	uint width, height;
	raw_ao.GetDimensions(width, height);
	int2 size = int2(width, height);
	for (uint idx = lane; idx < 100; idx += 64)
	{
		int2 q = int2(idx % 10, idx / 10);
		int2 tap = int2(group.xy) * 8 + q - 1;
		if (all(tap >= 0) && all(tap < size))
		{
			tile_nd[q.y][q.x] = load_surface(curr_surface, tap);
			tile_raw[q.y][q.x] = raw_ao.Load(int3(tap, 0));
		}
		else
			tile_nd[q.y][q.x] = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = rtao_frame_pixel(int2(tid.xy), params.pixel_scale, params.full_size, frame.viewport_origin);
	int2 center = int2(local.xy) + 1;
	float4 nd = tile_nd[center.y][center.x];
	float depth = nd.w;
	float raw = tile_raw[center.y][center.x];
	float2 result = float2(raw, 1);
	if (depth > 0 && params.has_history != 0)
	{
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 N = nd.xyz;
		float threshold = max(0.02, 0.02 * length(frame.camera_position.xyz - world));
		float weight_sum;
		float2 vel = work_velocity.Load(int3(params.pixel_scale == 1 ? pixel : int2(tid.xy), 0));
		SurfaceReprojection reproj = reproject_surface(pixel, vel, world, frame, prev_frame);
		float2 history_sum = gather_surface_history(prev_surface, prev_history, reproj, world, N, threshold,
				frame, prev_frame, params.pixel_scale, params.full_size, size, weight_sum).xy;
		if (weight_sum > 1e-4)
		{
			float2 history = history_sum / weight_sum;
			float count = min(history.y + 1, params.max_history);
			float alpha = 1 - pow(1 - rcp(params.max_history), params.dt_scale);
			if (count < params.max_history)
				alpha = max(alpha, rcp(count));
			if (count >= 4)
			{
				float4 curr_plane_c = surface_plane_coeffs(world, N, frame);
				float sum = raw, sum2 = raw * raw, taps = 1;
				for (int y = -1; y <= 1; ++y)
					for (int x = -1; x <= 1; ++x)
					{
						int2 tap = int2(tid.xy) + int2(x, y);
						int2 q = center + int2(x, y);
						float4 tap_nd = tile_nd[q.y][q.x];
						if ((x == 0 && y == 0) || tap_nd.w <= 0 || dot(N, tap_nd.xyz) <= 0.75)
							continue;
						int2 p = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, frame.viewport_origin);
						if (!plane_compatible(curr_plane_c, frame.inv_view_projection[3], surface_clip(p, tap_nd.w, frame), threshold))
							continue;
						float v = tile_raw[q.y][q.x];
						sum += v;
						sum2 += v * v;
						taps++;
					}
				float clamp_weight = saturate((taps - 4.0) / 5.0) * 0.25;
				if (clamp_weight > 0.0)
				{
					float mean = sum / taps;
					float extent = 2 * sqrt(max(0, sum2 / taps - mean * mean));
					history.x = lerp(history.x, clamp(history.x, mean - extent, mean + extent), clamp_weight);
				}
			}
			result = float2(lerp(history.x, raw, alpha), count);
		}
	}
	curr_history[tid.xy] = result;
}
