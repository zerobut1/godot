import common;

struct Region { int2 origin; int2 size; };
[[vk::push_constant]] ConstantBuffer<Region> region;
[[vk::binding(0, 0)]] Texture2D<float4> hdr;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> histogram;
groupshared uint local_bins[256];

[shader("compute")]
[numthreads(16, 16, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint lane : SV_GroupIndex)
{
	local_bins[lane] = 0;
	GroupMemoryBarrierWithGroupSync();
	if (all(tid.xy < uint2(region.size)))
	{
		float3 color = hdr.Load(int3(region.origin + int2(tid.xy), 0)).rgb;
		float luma = dot(max(sanitize_finite(color), 0.0), float3(0.2126, 0.7152, 0.0722));
		uint bin = luma < 1e-4 ? 0 : uint(saturate((log2(max(luma, 1e-4)) + 12.0) / 24.0) * 254.0 + 1.0);
		float2 offset = (tid.xy + 0.5) / region.size - 0.5;
		uint weight = 1 + uint(round(3.0 * exp(-4.0 * dot(offset, offset))));
		InterlockedAdd(local_bins[bin], weight);
	}
	GroupMemoryBarrierWithGroupSync();
	if (local_bins[lane] > 0)
		InterlockedAdd(histogram[lane], local_bins[lane]);
}
