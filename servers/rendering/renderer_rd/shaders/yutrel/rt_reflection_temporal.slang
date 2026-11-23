import common;
import rtao_common;
import rt_reflection_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float> scene_depth;
[[vk::binding(4, 0)]] Texture2D<float4> raw_reflection;
[[vk::binding(5, 0)]] Texture2D<float4> prev_history;
[[vk::binding(6, 0)]] Texture2D<float> prev_depth;
[[vk::binding(7, 0)]] Texture2D<float4> prev_normal_roughness;
[[vk::binding(8, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> curr_history;
[[vk::binding(9, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> curr_depth;
[[vk::binding(10, 0)]] [[vk::image_format("rgba16")]] RWTexture2D<float4> curr_normal_roughness;
[[vk::binding(11, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(12, 0)]] Texture2D<float> prev_hit_dist;
[[vk::binding(13, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> curr_hit_dist;

struct TemporalParams
{
	int2 full_size;
	uint pixel_scale, has_history;
	float max_history, dt_scale;
};
[[vk::push_constant]] ConstantBuffer<TemporalParams> params;

float4 normal_roughness(int2 pixel)
{
	return float4(gbuffer_b.Load(int3(pixel, 0)).rgb, gbuffer_c.Load(int3(pixel, 0)).r);
}

bool gather_history(float3 projected, float3 world, float3 N, float r, float threshold, int2 size,
		out float4 history, out float hit_dist, out float2 prev_work)
{
	history = 0;
	hit_dist = 0;
	prev_work = 0;
	float4 clip = mul_4x4(prev_frame.view_projection, float4(projected, 1));
	if (clip.w <= 0 || any(abs(clip.xy) > clip.w))
		return false;
	float2 prev_full = (clip.xy / clip.w * 0.5 + 0.5) / prev_frame.inv_viewport_size;
	prev_work = (prev_full - 0.5) / float(params.pixel_scale);
	int2 base = int2(floor(prev_work));
	float2 f = frac(prev_work);
	float weights = 0;
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 tap = base + int2(x, y);
			if (any(tap < 0) || any(tap >= size))
				continue;
			int3 coord = int3(tap, 0);
			float4 h = prev_history.Load(coord);
			int2 pixel = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, prev_frame.viewport_origin);
			// Test the actual reflector plane even when projecting a virtual point behind it.
			if (h.a <= 0 || !reflection_compatible(world, N, r, threshold, pixel, prev_depth.Load(coord), prev_normal_roughness.Load(coord), prev_frame))
				continue;
			float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			history += float4(reflection_compress(h.rgb), h.a) * w;
			hit_dist += prev_hit_dist.Load(coord) * w;
			weights += w;
		}
	if (weights < 1e-4)
		return false;
	history /= weights;
	hit_dist /= weights;
	return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	raw_reflection.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 center = int2(tid.xy);
	int2 pixel = rtao_frame_pixel(center, params.pixel_scale, params.full_size, frame.viewport_origin);
	float depth = scene_depth.Load(int3(pixel, 0));
	float4 nr = normal_roughness(pixel);
	curr_depth[center] = depth;
	curr_normal_roughness[center] = nr;
	float4 raw = raw_reflection.Load(int3(center, 0));
	float4 result = float4(raw.rgb, raw.a >= 0 ? 1 : 0);
	float distance = raw.a;
	if (raw.a >= 0 && params.has_history != 0)
	{
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 N = reflection_normal(nr);
		float threshold = max(0.02, 0.02 * length(frame.camera_position.xyz - world));
		float4 history = 0;
		float prev_distance = 0;
		float2 prev_work = 0;
		bool valid = false;
		if (raw.a > 0 && nr.a < 0.25)
		{
			float3 virtual_world = world - reflection_view(world, frame) * raw.a;
			valid = gather_history(virtual_world, world, N, nr.a, threshold, size, history, prev_distance, prev_work);
		}
		if (!valid)
			valid = gather_history(world, world, N, nr.a, threshold, size, history, prev_distance, prev_work);
		if (valid)
		{
			float3 current = reflection_compress(raw.rgb);
			float3 sum = current, sum2 = current * current;
			float taps = 1;
			for (int y = -1; y <= 1; ++y)
				for (int x = -1; x <= 1; ++x)
				{
					int2 tap = center + int2(x, y);
					if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size))
						continue;
					float4 sample = raw_reflection.Load(int3(tap, 0));
					int2 p = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, frame.viewport_origin);
					if (sample.a < 0 || !reflection_compatible(world, N, nr.a, threshold, p, scene_depth.Load(int3(p, 0)), normal_roughness(p), frame))
						continue;
					float3 c = reflection_compress(sample.rgb);
					sum += c;
					sum2 += c * c;
					taps++;
				}
			float motion = saturate(length(prev_work - center));
			float3 mean = sum / taps;
			// A sudden change in the reflected layer can disocclude a stationary
			// reflector. Glossy VNDF samples naturally have a wider distance spread.
			float distance_delta = abs(prev_distance - raw.a) / max(max(prev_distance, raw.a), 0.01);
			float change = max(motion, (1 - smoothstep(0.04, 0.25, nr.a)) * smoothstep(0.25, 0.75, distance_delta));
			float3 extent = lerp(2.0, 1.0, change) * sqrt(max(0, sum2 / taps - mean * mean));
			float3 prior = lerp(history.rgb, clamp(history.rgb, mean - extent, mean + extent), lerp(0.25, 1.0, change));
			result.a = min(history.a + 1, params.max_history);
			float alpha = max(rcp(result.a), 1 - pow(1 - rcp(params.max_history), params.dt_scale));
			// A deterministic mirror sample resolves moving high-contrast edges without a history trail.
			if (nr.a < 0.04)
				alpha = max(alpha, max(change, saturate(8 * length(prior - current))));
			result.rgb = reflection_decompress(lerp(prior, current, alpha));
			distance = lerp(prev_distance, raw.a, alpha);
		}
	}
	curr_history[center] = result;
	curr_hit_dist[center] = distance;
}
