import common;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float> scene_depth;
[[vk::binding(4, 0)]] Texture2D<float> raw_ao;
[[vk::binding(5, 0)]] Texture2D<float2> prev_history;
[[vk::binding(6, 0)]] Texture2D<float> prev_depth;
[[vk::binding(7, 0)]] Texture2D<float4> prev_normal;
[[vk::binding(8, 0)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> curr_history;
[[vk::binding(9, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> curr_depth;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16")]] RWTexture2D<float4> curr_normal;

struct TemporalParams
{
	int2 full_size;
	uint pixel_scale;
	uint has_history;
	float max_history, dt_scale;
};
[[vk::push_constant]] ConstantBuffer<TemporalParams> params;

bool compatible(float3 world, float3 N, float threshold, int2 pixel, float depth, float4 normal, FrameData tap_frame)
{
	if (depth <= 0 || dot(N, normalize_or(normal.rgb * 2 - 1, 0)) <= 0.9)
		return false;
	float3 tap_world = reconstruct_world_position(pixel + 0.5, depth, tap_frame);
	return abs(dot(world - tap_world, N)) < threshold;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	raw_ao.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = rtao_frame_pixel(int2(tid.xy), params.pixel_scale, params.full_size, frame.viewport_origin);
	float depth = scene_depth.Load(int3(pixel, 0));
	float4 normal = gbuffer_b.Load(int3(pixel, 0));
	curr_depth[tid.xy] = depth;
	curr_normal[tid.xy] = normal;
	float raw = raw_ao.Load(int3(tid.xy, 0));
	float2 result = float2(raw, 1);
	if (depth > 0 && params.has_history != 0)
	{
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 N = normalize_or(normal.rgb * 2 - 1, float3(0, 0, 1));
		float threshold = max(0.02, 0.02 * length(frame.camera_position.xyz - world));
		float4 clip = mul_4x4(prev_frame.view_projection, float4(world, 1));
		float2 history_sum = 0;
		float weight_sum = 0;
		if (clip.w > 0 && clip.z >= 0 && clip.z <= clip.w && all(abs(clip.xy) <= clip.w))
		{
			// History is viewport-local. Add the previous origin only for world reconstruction.
			float2 prev_full = (clip.xy / clip.w * 0.5 + 0.5) / prev_frame.inv_viewport_size;
			float2 prev_work = (prev_full - 0.5) / float(params.pixel_scale);
			int2 base = int2(floor(prev_work));
			float2 f = frac(prev_work);
			for (int y = 0; y < 2; ++y)
				for (int x = 0; x < 2; ++x)
				{
					int2 tap = base + int2(x, y);
					if (any(tap < 0) || any(tap >= size))
						continue;
					int3 coord = int3(tap, 0);
					int2 tap_pixel = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, prev_frame.viewport_origin);
					if (!compatible(world, N, threshold, tap_pixel,
							prev_depth.Load(coord), prev_normal.Load(coord), prev_frame))
						continue;
					float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
					history_sum += w * prev_history.Load(coord);
					weight_sum += w;
				}
		}
		if (weight_sum > 1e-4)
		{
			float2 history = history_sum / weight_sum;
			float count = min(history.y + 1, params.max_history);
			float alpha = 1 - pow(1 - rcp(params.max_history), params.dt_scale);
			if (count < params.max_history)
				alpha = max(alpha, rcp(count));
			if (count >= 4)
			{
				float sum = raw, sum2 = raw * raw, taps = 1;
				for (int y = -1; y <= 1; ++y)
					for (int x = -1; x <= 1; ++x)
					{
						int2 tap = int2(tid.xy) + int2(x, y);
						if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size))
							continue;
						int2 p = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, frame.viewport_origin);
						if (!compatible(world, N, threshold, p, scene_depth.Load(int3(p, 0)), gbuffer_b.Load(int3(p, 0)), frame))
							continue;
						float v = raw_ao.Load(int3(tap, 0));
						sum += v;
						sum2 += v * v;
						taps++;
					}
				float mean = sum / taps;
				float extent = 2 * sqrt(max(0, sum2 / taps - mean * mean));
				// A single noisy neighborhood should only gently shorten a stale trail.
				history.x = lerp(history.x, clamp(history.x, mean - extent, mean + extent), 0.25);
			}
			result = float2(lerp(history.x, raw, alpha), count);
		}
	}
	curr_history[tid.xy] = result;
}
