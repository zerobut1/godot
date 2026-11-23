import common;
import rtao_common;
import rt_reflection_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2, 0)]] Texture2D<float> full_depth;
[[vk::binding(3, 0)]] Texture2D<float4> filtered_reflection;
[[vk::binding(4, 0)]] Texture2D<float> half_depth;
[[vk::binding(5, 0)]] Texture2D<float4> half_normal_roughness;
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> reflection_texture;
[[vk::binding(7, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(8, 0)]] Texture2D<float4> gbuffer_a;

struct UpsampleParams
{
	float max_roughness;
};
[[vk::push_constant]] ConstantBuffer<UpsampleParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	reflection_texture.GetDimensions(width, height);
	int2 full_size = int2(width, height);
	if (any(tid.xy >= uint2(full_size)))
		return;
	int2 pixel = int2(tid.xy) + int2(frame.viewport_origin);
	int3 coord = int3(pixel, 0);
	float depth = full_depth.Load(coord);
	float r = gbuffer_c.Load(coord).r;
	if (depth <= 0 || r >= params.max_roughness || gbuffer_a.Load(coord).a == 0)
	{
		reflection_texture[tid.xy] = 0;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = reflection_normal(gbuffer_b.Load(coord));
	float inv_sigma = rtao_inv_sigma(world, frame.camera_position.xyz);
	float2 hf = float2(tid.xy) * 0.5;
	int2 base = int2(floor(hf));
	float2 f = frac(hf);
	float3 sum = 0;
	float weights = 0, nearest_dist = 1e10;
	float4 nearest = 0;
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 tap = min(base + int2(x, y), (full_size - 1) / 2);
			int3 tc = int3(tap, 0);
			float4 sample = filtered_reflection.Load(tc);
			float4 nr = half_normal_roughness.Load(tc);
			int2 p = rtao_frame_pixel(tap, 2, full_size, frame.viewport_origin);
			float z = half_depth.Load(tc);
			if (sample.a <= 0 || !reflection_compatible(world, N, r, 2 / inv_sigma, p, z, nr, frame))
				continue;
			float2 delta = float2(p - pixel);
			float dsq = dot(delta, delta);
			if (dsq < nearest_dist)
			{
				nearest = sample;
				nearest_dist = dsq;
			}
			float3 tap_world = reconstruct_world_position(p + 0.5, z, frame);
			float w = reflection_weight(world, N, r, tap_world, nr, inv_sigma);
			w *= (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			sum += w * sample.rgb;
			weights += w;
		}
	reflection_texture[tid.xy] = weights > 1e-4 ? float4(sum / weights, 1) : nearest;
}
