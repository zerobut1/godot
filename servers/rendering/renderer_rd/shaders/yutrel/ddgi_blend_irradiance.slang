import ddgi_common;
import ddgi_probe_blend;

[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2DArray<float4> atlas;

float max_component(float3 c) { return max(c.x, max(c.y, c.z)); }

// A high hysteresis can make an entire update smaller than one FP16 ULP.
// Stochastically round the final store so repeated updates retain their mean.
// Use one threshold for RGB to keep neutral illumination neutral.
float3 round_history_fp16(float3 value, uint3 texel)
{
	uint seed = asuint(volume.ray_rotation.x) ^ (texel.x * 1973u + texel.y * 9277u + texel.z * 26699u);
	seed = seed * 747796405u + 2891336453u;
	uint word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
	float threshold = float(((word >> 22u) ^ word) >> 8u) / 16777216.0;
	// An FP16 bin is 2^(exponent - 10), with fixed spacing for subnormals.
	float3 bin = asfloat(asuint(max(value, exp2(-14.0))) & 0x7f800000u) / 1024.0;
	float3 scaled = value / bin;
	return bin * (floor(scaled) + select(frac(scaled) > threshold, 1.0, 0.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID, uint lane : SV_GroupIndex, uint3 tid : SV_DispatchThreadID)
{
	if (!blend_probe_is_active(group))
		return;
	uint backfaces;
	float3 sample = integrate_rays(group, local.xy, lane, 8, false, backfaces);
	bool border = is_border(local.xy, 8);
	if (!border)
	{
		if (backfaces != 0 && backfaces >= uint(blend_ray_count() * volume.backface_threshold))
		{
			if (volume.reset_history != 0)
				atlas[tid] = 0;
		}
		else
		{
			sample = pow(max(sample, 0), 1.0 / volume.irradiance_gamma);
			float3 prev = volume.reset_history != 0 ? float3(0) : atlas[tid].rgb;
			bool first = all(prev == 0);
			float h = first ? 0 : volume.hysteresis;
			if (!first && max_component(prev - sample) > volume.irradiance_threshold)
				h = pow(h, 49.0);
			float3 delta = sample - prev;
			if (!first && ddgi_luminance(delta) > volume.brightness_threshold)
				delta *= 0.25;
			float3 step = (1 - h) * delta;
			atlas[tid] = float4(round_history_fp16(max(prev + step, 0), tid), 1);
		}
	}
	AllMemoryBarrierWithGroupSync();
	if (border)
		atlas[tid] = atlas[uint3(group.xy * 8 + border_source(local.xy, 8), group.z)];
}
