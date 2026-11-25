import common;
import rtao_common;

[[vk::binding(0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2)]] Texture2D<float> scene_depth;
[[vk::binding(3)]] Texture2D<float2> full_velocity;
[[vk::binding(4)]] [[vk::image_format("rg32ui")]] RWTexture2D<uint2> out_surface;
[[vk::binding(5)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> out_velocity;

struct PrepareParams
{
	int2 full_size;
	uint pixel_scale, write_velocity;
};
[[vk::push_constant]] ConstantBuffer<PrepareParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	int2 size = (params.full_size + int(params.pixel_scale) - 1) / int(params.pixel_scale);
	if (any(int2(tid.xy) >= size))
		return;
	int2 pixel = rtao_frame_pixel(int2(tid.xy), params.pixel_scale, params.full_size, frame.viewport_origin);
	float depth = scene_depth.Load(int3(pixel, 0));
	if (depth <= 0)
	{
		out_surface[tid.xy] = uint2(0);
		if (params.write_velocity != 0)
			out_velocity[tid.xy] = float2(0);
		return;
	}
	float3 N = normalize_or(gbuffer_b.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	out_surface[tid.xy] = pack_surface(N, depth);
	if (params.write_velocity != 0)
		out_velocity[tid.xy] = full_velocity.Load(int3(pixel, 0));
}
