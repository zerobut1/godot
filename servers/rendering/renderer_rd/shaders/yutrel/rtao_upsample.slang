import common;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> full_normal;
[[vk::binding(2, 0)]] Texture2D<float> full_depth;
[[vk::binding(3, 0)]] Texture2D<float> half_ao;
[[vk::binding(4, 0)]] Texture2D<uint2> half_surface;
[[vk::binding(6, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> output_ao;

struct UpsampleParams
{
	float intensity;
};
[[vk::push_constant]] ConstantBuffer<UpsampleParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	uint width, height;
	output_ao.GetDimensions(width, height);
	int2 full_size = int2(width, height);
	int2 pixel = int2(tid.xy) + int2(frame.viewport_origin);
	int2 tile_origin = int2(group.xy) * 4;
	cache_half_visibility(lane, tile_origin, full_size, frame, half_surface, half_ao);
	if (any(tid.xy >= uint2(full_size)))
		return;
	float depth = full_depth.Load(int3(pixel, 0));
	if (depth <= 0)
	{
		output_ao[tid.xy] = 1;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = normalize_or(full_normal.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	float inv_sigma = rtao_inv_sigma(world, frame.camera_position.xyz);
	float ao = upsample_visibility(int2(tid.xy), tile_origin, world, N, depth, inv_sigma);
	output_ao[tid.xy] = params.intensity > 0 ? pow(ao, params.intensity) : 1;
}
