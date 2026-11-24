import common;
import temporal_reprojection;
import rt_shadow_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float> scene_depth;
[[vk::binding(4, 0)]] Texture2D<float2> raw_shadow;
[[vk::binding(5, 0)]] Texture2D<float4> prev_history;
[[vk::binding(6, 0)]] Texture2D<float> prev_depth;
[[vk::binding(7, 0)]] Texture2D<float4> prev_normal;
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> curr_history;
[[vk::binding(9, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> curr_depth;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16")]] RWTexture2D<float4> curr_normal;

[[vk::binding(11, 0)]] Texture2D<float2> velocity;

struct TemporalParams
{
	ShadowGrid grid;
	uint has_history;
	float max_history, dt_scale;
	uint write_surfaces;
};
[[vk::push_constant]] ConstantBuffer<TemporalParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	raw_shadow.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = params.grid.frame_pixel(int2(tid.xy), frame);
	float depth = scene_depth.Load(int3(pixel, 0));
	float4 normal = gbuffer_b.Load(int3(pixel, 0));
	if (params.write_surfaces != 0)
	{
		curr_depth[tid.xy] = depth;
		curr_normal[tid.xy] = normal;
	}
	float2 raw = raw_shadow.Load(int3(tid.xy, 0));
	float4 result = float4(raw, 1, 0);
	if (depth > 0)
	{
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 N = normalize_or(normal.rgb * 2 - 1, float3(0, 0, 1));
		float threshold = max(0.02, 0.02 * length(frame.camera_position.xyz - world));
		float sum = raw.x, sum2 = raw.x * raw.x, taps = 1;
		for (int y = -1; y <= 1; ++y)
			for (int x = -1; x <= 1; ++x)
			{
				int2 tap = int2(tid.xy) + int2(x, y);
				if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size))
					continue;
				int2 p = params.grid.frame_pixel(tap, frame);
				if (!shadow_surface_compatible(world, N, threshold, p, scene_depth.Load(int3(p, 0)), gbuffer_b.Load(int3(p, 0)), frame))
					continue;
				float2 v = raw_shadow.Load(int3(tap, 0));
				result.g = max(result.g, v.g);
				sum += v.x;
				sum2 += v.x * v.x;
				taps++;
			}
		SurfaceReprojection reproj = reproject_surface(pixel, velocity.Load(int3(pixel, 0)), world, frame, prev_frame);
		float3 history_sum = 0;
		float weight_sum = 0;
		if (params.has_history != 0 && reproj.visible)
		{
			// History is viewport-local. Add the previous origin only for world reconstruction.
			float2 prev_full = reproj.prev_uv / prev_frame.inv_viewport_size;
			float2 prev_work = (prev_full - 0.5) / float(params.grid.pixel_scale);
			int2 base = int2(floor(prev_work));
			float2 f = frac(prev_work);
			for (int y = 0; y < 2; ++y)
				for (int x = 0; x < 2; ++x)
				{
					int2 tap = base + int2(x, y);
					if (any(tap < 0) || any(tap >= size))
						continue;
					int3 coord = int3(tap, 0);
					int2 tap_pixel = params.grid.frame_pixel(tap, prev_frame);
					float tap_depth = prev_depth.Load(coord);
					float4 tap_normal = prev_normal.Load(coord);
					bool valid = reproj.moving ? moving_history_tap(world, N, tap_pixel, tap_depth, tap_normal, frame, prev_frame)
							: shadow_surface_compatible(world, N, threshold, tap_pixel, tap_depth, tap_normal, prev_frame);
					if (!valid)
						continue;
					float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
					history_sum += w * prev_history.Load(coord).rgb;
					weight_sum += w;
				}
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
