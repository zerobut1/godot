import common;
import rt_shadow_common;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> full_normal;
[[vk::binding(2, 0)]] Texture2D<float> full_depth;
[[vk::binding(3, 0)]] Texture2D<float> input_shadow;
[[vk::binding(4, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> shadow_mask;

[[vk::binding(5, 0)]] Texture2D<uint2> half_surface;

struct UpsampleParams
{
	ShadowGrid grid;
	float opacity;
};
[[vk::push_constant]] ConstantBuffer<UpsampleParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	int2 full_size = params.grid.full_size;
	int2 pixel = int2(tid.xy) + int2(frame.viewport_origin);
	if (params.grid.pixel_scale == 1)
	{
		if (all(tid.xy < uint2(full_size)))
			shadow_mask[pixel] = lerp(1.0, input_shadow.Load(int3(tid.xy, 0)), params.opacity);
		return;
	}
	int2 tile_origin = int2(group.xy) * 4;
	cache_half_visibility(lane, tile_origin, full_size, frame, half_surface, input_shadow);
	if (any(tid.xy >= uint2(full_size)))
		return;
	float depth = full_depth.Load(int3(pixel, 0));
	if (depth <= 0)
	{
		shadow_mask[pixel] = 1;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = normalize_or(full_normal.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	float inv_sigma = rtao_inv_sigma(world, frame.camera_position.xyz);
	float filtered = upsample_visibility(int2(tid.xy), tile_origin, world, N, depth, inv_sigma);
	shadow_mask[pixel] = lerp(1.0, filtered, params.opacity);
}
