import bloom_common;

[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> dest;
[[vk::binding(2, 0)]] SamplerState linear_clamp;
struct DownParams { int2 size; };
[[vk::push_constant]] ConstantBuffer<DownParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	if (any(tid.xy >= uint2(params.size))) return;
	uint2 src_size;
	source.GetDimensions(src_size.x, src_size.y);
	float3 c = bloom_downfilter(source, linear_clamp, (tid.xy + 0.5) / params.size,
			1.0 / float2(src_size), 0.0, 1.0, false, 1.0, 0.0, 0.0);
	dest[tid.xy] = float4(c, 1.0);
}
