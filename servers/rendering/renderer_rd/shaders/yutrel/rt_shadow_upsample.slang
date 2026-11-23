import common;
import rt_shadow_common;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> full_normal;
[[vk::binding(2, 0)]] Texture2D<float> full_depth;
[[vk::binding(3, 0)]] Texture2D<float> input_shadow;
[[vk::binding(4, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> shadow_mask;

struct UpsampleParams
{
	ShadowGrid grid;
	float opacity;
};
[[vk::push_constant]] ConstantBuffer<UpsampleParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	int2 full_size = params.grid.full_size;
	if (any(tid.xy >= uint2(full_size)))
		return;
	int2 pixel = int2(tid.xy) + int2(frame.viewport_origin);
	if (params.grid.pixel_scale == 1)
	{
		shadow_mask[pixel] = lerp(1.0, input_shadow.Load(int3(tid.xy, 0)), params.opacity);
		return;
	}
	float depth = full_depth.Load(int3(pixel, 0));
	if (depth <= 0)
	{
		shadow_mask[pixel] = 1;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = normalize_or(full_normal.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	float inv_sigma = rtao_inv_sigma(world, frame.camera_position.xyz);
	int2 half_size = (full_size + 1) / 2;
	float2 hf = float2(tid.xy) * 0.5;
	int2 base = int2(floor(hf));
	float2 f = frac(hf);
	float sum = 0, weight_sum = 0;
	float nearest_visibility = 1, nearest_delta = 2;
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 tap = min(base + int2(x, y), half_size - 1);
			int2 tap_pixel = params.grid.frame_pixel(tap, frame);
			float hz = full_depth.Load(int3(tap_pixel, 0));
			if (hz <= 0)
				continue;
			float visibility = input_shadow.Load(int3(tap, 0));
			float delta = abs(hz - depth);
			if (delta < nearest_delta)
			{
				nearest_delta = delta;
				nearest_visibility = visibility;
			}
			float3 tap_world = reconstruct_world_position(tap_pixel + 0.5, hz, frame);
			float3 hn = normalize_or(full_normal.Load(int3(tap_pixel, 0)).rgb * 2 - 1, 0);
			float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			w *= rtao_surface_weight(world, N, tap_world, hn, inv_sigma);
			sum += w * visibility;
			weight_sum += w;
		}
	float filtered = saturate(weight_sum > 1e-4 ? sum / weight_sum : nearest_visibility);
	shadow_mask[pixel] = lerp(1.0, filtered, params.opacity);
}
