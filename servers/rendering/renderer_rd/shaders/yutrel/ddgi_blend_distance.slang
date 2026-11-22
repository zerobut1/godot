import ddgi_probe_blend;

[[vk::binding(2, 0)]] [[vk::image_format("rg16f")]] RWTexture2DArray<float2> atlas;

[shader("compute")]
[numthreads(16, 16, 1)]
void compute_main(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint lane : SV_GroupIndex, uint3 tid : SV_DispatchThreadID)
{
	if (!blend_probe_is_active(group))
		return;
	uint backfaces;
	float2 moments = integrate_rays(group, local.xy, lane, 16, true, backfaces).xy;
	bool border = is_border(local.xy, 16);
	if (!border)
	{
		float2 prev = volume.reset_history != 0 ? float2(0) : atlas[tid];
		atlas[tid] = lerp(moments, prev, all(prev == 0) ? 0 : volume.hysteresis);
	}
	AllMemoryBarrierWithGroupSync();
	if (border)
		atlas[tid] = atlas[uint3(group.xy * 16 + border_source(local.xy, 16), group.z)];
}
