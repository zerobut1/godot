import common;

struct CascadeMatrix { float4 rows[4]; };
public struct CSMShadowData
{
	CascadeMatrix vp[4];
	float4 splits;
	float4 world_texel_size;
	float4 depth_bias;
	float normal_bias;
	float blur_radius;
	float opacity;
	float inv_resolution;
	float fade_start;
	float max_distance;
	uint cascade_count;
	uint first_layer;
	uint blend_splits;
	float camera_near;
	float2 padding;
};

static const float2 poisson[16] = {
	float2(-0.942, -0.399), float2(0.946, -0.769), float2(-0.094, -0.929), float2(0.345, 0.294),
	float2(-0.916, 0.458), float2(-0.815, -0.879), float2(-0.382, 0.277), float2(0.974, 0.756),
	float2(0.443, -0.975), float2(0.537, -0.474), float2(-0.265, -0.419), float2(0.792, 0.191),
	float2(-0.242, 0.997), float2(-0.814, 0.914), float2(0.200, 0.786), float2(0.144, -0.141)
};

float sample_cascade(CSMShadowData csm, Texture2DArray<float> csm_shadow_array, SamplerComparisonState comparison_sampler, uint index, float3 world, float3 normal, float2 rotation)
{
	float3 receiver = world + normal * (csm.normal_bias * csm.world_texel_size[index]);
	float4 clip = mul_4x4(csm.vp[index].rows, float4(receiver, 1.0));
	float3 ndc = clip.xyz / clip.w;
	float2 uv = ndc.xy * 0.5 + 0.5;
	if (any(uv < 0.0) || any(uv > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
		return 1.0;
	float reference = saturate(ndc.z + csm.depth_bias[index]);
	float visibility = 0.0;
	[unroll] for (int tap = 0; tap < 16; tap++)
	{
		float2 delta = rotation * poisson[tap].x + float2(-rotation.y, rotation.x) * poisson[tap].y;
		float2 sample_uv = uv + delta * (csm.blur_radius * csm.inv_resolution);
		visibility += csm_shadow_array.SampleCmpLevelZero(comparison_sampler, float3(sample_uv, csm.first_layer + index), reference);
	}
	return visibility / 16.0;
}

public struct CascadeSelection
{
	public uint index;
	public uint next;
	public float blend;
	public float strength;
};

public CascadeSelection select_cascade(CSMShadowData csm, float distance)
{
	uint index = 0;
	while (index + 1 < csm.cascade_count && distance > csm.splits[index])
		index++;
	uint next = min(index + 1, csm.cascade_count - 1);
	float end = csm.splits[index];
	float start = index == 0 ? csm.camera_near : csm.splits[index - 1];
	float blend = csm.blend_splits != 0 && index != next ? smoothstep(0.0, 1.0,
			1.0 - (end - distance) / max(0.1 * (end - start), 1e-5)) : 0.0;
	float strength = smoothstep(0.0, 1.0, (csm.max_distance - distance) / max(csm.max_distance - csm.fade_start, 1e-5));
	return { index, next, blend, strength };
}

public float evaluate_csm_shadow(CSMShadowData csm, Texture2DArray<float> csm_shadow_array, SamplerComparisonState comparison_sampler,
		CascadeSelection cascade, float3 world, float3 normal, float2 pixel)
{
	if (cascade.strength <= 0.0)
		return 1.0;
	float noise = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
	float2 rotation;
	sincos(6.2831853 * noise, rotation.y, rotation.x);
	float visibility = sample_cascade(csm, csm_shadow_array, comparison_sampler, cascade.index, world, normal, rotation);
	if (cascade.blend > 0.0)
		visibility = lerp(visibility, sample_cascade(csm, csm_shadow_array, comparison_sampler, cascade.next, world, normal, rotation), cascade.blend);
	return lerp(1.0, visibility, cascade.strength * csm.opacity);
}
