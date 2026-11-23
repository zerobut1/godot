import common;
import rtao_common;
import rt_reflection_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> work_normal_roughness;
[[vk::binding(2, 0)]] Texture2D<float> work_depth;
[[vk::binding(3, 0)]] Texture2D<float4> input_reflection;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> output_reflection;
[[vk::binding(5, 0)]] Texture2D<float> hit_dist;

struct FilterParams
{
	int2 full_size;
	uint pixel_scale;
	int step_size;
	uint is_final;
};
[[vk::push_constant]] ConstantBuffer<FilterParams> params;

float4 compressed_sample(int2 tap)
{
	float4 sample = input_reflection.Load(int3(tap, 0));
	// Preserve 1 - luma(compressed) separately: subtracting from one after an FP16
	// store would erase the scale of bright HDR highlights.
	return params.step_size == 1 ? float4(sample.rgb, 1) / (1 + reflection_luma(sample.rgb)) : sample;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	hit_dist.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 center = int2(tid.xy);
	int3 coord = int3(center, 0);
	float distance = hit_dist.Load(coord);
	if (distance < 0)
	{
		output_reflection[center] = 0;
		return;
	}
	float4 result = compressed_sample(center);
	float4 nr = work_normal_roughness.Load(coord);
	if (nr.a >= 0.04)
	{
		float depth = work_depth.Load(coord);
		int2 pixel = rtao_frame_pixel(center, params.pixel_scale, params.full_size, frame.viewport_origin);
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 adjacent = reconstruct_world_position(pixel + float2(params.pixel_scale + 0.5, 0.5), depth, frame);
		float cone = clamp(min(distance, 100.0) * nr.a * nr.a / max(length(adjacent - world), 1e-6), 0, 24);
		if (cone >= 0.5)
		{
			float3 N = reflection_normal(nr);
			float inv_sigma = rtao_inv_sigma(world, frame.camera_position.xyz);
			float sigma = 0.5 * cone;
			float spatial = exp(-float(params.step_size * params.step_size) / (2 * sigma * sigma));
			float weights = 1;
			for (int y = -1; y <= 1; ++y)
				for (int x = -1; x <= 1; ++x)
				{
					int2 tap = center + int2(x, y) * params.step_size;
					if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size) || hit_dist.Load(int3(tap, 0)) < 0)
						continue;
					int2 p = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, frame.viewport_origin);
					float3 tap_world = reconstruct_world_position(p + 0.5, work_depth.Load(int3(tap, 0)), frame);
					float w = reflection_weight(world, N, nr.a, tap_world, work_normal_roughness.Load(int3(tap, 0)), inv_sigma);
					w *= x == 0 || y == 0 ? spatial : spatial * spatial;
					result += w * compressed_sample(tap);
					weights += w;
				}
			result /= weights;
		}
	}
	output_reflection[center] = params.is_final != 0 ? float4(clamp(result.rgb / max(result.a, 1e-5), 0, 65504), 1) : result;
}
