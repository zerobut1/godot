import common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2, 0)]] Texture2D<float> scene_depth;
[[vk::binding(3, 0)]] Texture2D<float2> input_ao;
[[vk::binding(4, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> output_ao;

struct FilterParams
{
	int step_size;
	uint is_final;
	float intensity;
};
[[vk::push_constant]] ConstantBuffer<FilterParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	output_ao.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = int2(tid.xy) + int2(frame.viewport_origin);
	float depth = scene_depth.Load(int3(pixel, 0));
	if (depth <= 0)
	{
		output_ao[tid.xy] = 1;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = normalize_or(gbuffer_b.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	float inv_sigma = rcp(max(0.015, 0.015 * length(frame.camera_position.xyz - world)));
	float2 center = input_ao.Load(int3(tid.xy, 0));
	float sum = 0.25 * center.r, weight_sum = 0.25;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			int2 tap = int2(tid.xy) + int2(x, y) * params.step_size;
			if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size))
				continue;
			int2 p = tap + int2(frame.viewport_origin);
			float z = scene_depth.Load(int3(p, 0));
			if (z <= 0)
				continue;
			float3 tap_world = reconstruct_world_position(p + 0.5, z, frame);
			float3 tap_N = normalize_or(gbuffer_b.Load(int3(p, 0)).rgb * 2 - 1, 0);
			float w = (x == 0 || y == 0) ? 0.125 : 0.0625;
			w *= exp(-abs(dot(tap_world - world, N)) * inv_sigma) * pow(saturate(dot(tap_N, N)), 64);
			float2 value = input_ao.Load(int3(tap, 0));
			if (params.step_size == 1)
				w *= min(1, value.g / center.g);
			sum += w * value.r;
			weight_sum += w;
		}
	float filtered = saturate(sum / weight_sum);
	if (params.is_final != 0)
		filtered = params.intensity > 0 ? pow(filtered, params.intensity) : 1;
	output_ao[tid.xy] = filtered;
}
