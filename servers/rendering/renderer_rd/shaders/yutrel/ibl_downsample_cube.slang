[[vk::binding(0, 0)]] Texture2D<float4> source_face;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> destination_face;

struct DownsampleParams
{
	uint size;
};
[[vk::push_constant]] ConstantBuffer<DownsampleParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= params.size))
	{
		return;
	}
	int3 p = int3(id.xy * 2, 0);
	destination_face[id.xy] = 0.25 * (source_face.Load(p)
			+ source_face.Load(p + int3(1, 0, 0))
			+ source_face.Load(p + int3(0, 1, 0))
			+ source_face.Load(p + int3(1, 1, 0)));
}
