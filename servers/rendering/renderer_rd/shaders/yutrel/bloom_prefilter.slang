import bloom_common;

[[vk::binding(0, 0)]] Texture2D<float4> source;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> dest;
[[vk::binding(2, 0)]] SamplerState linear_clamp;
[[vk::binding(3, 0)]] Texture2D<float2> exposure_tex;
struct PrefilterParams { int2 origin, size; float exposure, threshold, soft_knee; uint use_auto_exposure; };
[[vk::push_constant]] ConstantBuffer<PrefilterParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint2 size, src_size;
	dest.GetDimensions(size.x, size.y);
	if (any(tid.xy >= size)) return;
	source.GetDimensions(src_size.x, src_size.y);
	float2 center = (params.origin + (tid.xy + 0.5) * params.size / size) / src_size;
	float active = params.use_auto_exposure != 0 ? exposure_tex.Load(int3(0,0,0)).r : params.exposure;
	float3 c = bloom_downfilter(source, linear_clamp, center, 1.0 / float2(src_size),
			(params.origin + 0.5) / src_size, (params.origin + params.size - 0.5) / src_size,
			true, active, params.threshold, params.soft_knee);
	dest[tid.xy] = float4(c, 1.0);
}
