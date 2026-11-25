[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> dest;
[[vk::binding(2, 0)]] SamplerState linear_clamp;
[[vk::binding(3, 0)]] Texture2D<float4> current;
struct UpParams { float scatter; };
[[vk::push_constant]] ConstantBuffer<UpParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint2 size, src_size;
	dest.GetDimensions(size.x, size.y);
	if (any(tid.xy >= size)) return;
	source.GetDimensions(src_size.x, src_size.y);
	float2 uv = (tid.xy + 0.5) / size;
	float3 tent = 0;
	[unroll] for (int y = -1; y <= 1; ++y)
		[unroll] for (int x = -1; x <= 1; ++x)
			tent += source.SampleLevel(linear_clamp, uv + float2(x, y) / src_size, 0).rgb * ((2 - abs(x)) * (2 - abs(y)) / 16.0);
	dest[tid.xy] = float4(lerp(current.Load(int3(tid.xy, 0)).rgb, tent, params.scatter), 1.0);
}
