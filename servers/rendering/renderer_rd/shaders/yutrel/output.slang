import common;
import color_grading_common;

[[vk::binding(0, 0)]] ConstantBuffer<OutputUniforms> params;
[[vk::binding(1, 0)]] Texture2D<float4> external_strip;
[[vk::binding(2, 0)]] SamplerState linear_clamp;
[[vk::binding(3, 0)]] Texture3D<float4> lut;
[[vk::binding(4, 0)]] Texture2D<float4> scene_color;
[[vk::binding(5, 0)]] Texture2D<float4> bloom;
[[vk::binding(6, 0)]] Texture2D<float2> exposure_tex;

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

float ign(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	float4 sampled = scene_color.Load(int3(int2(position.xy), 0));
	float exposure = params.use_auto_exposure != 0 ? exposure_tex.Load(int3(0, 0, 0)).r : params.exposure;
	float3 c = min(max(sanitize_finite(sampled.rgb), 0.0) * exposure, 65504.0);
	float2 uv = (position.xy - params.origin) * params.inv_size;
	if (params.bloom_intensity > 0.0)
		c += bloom.SampleLevel(linear_clamp, uv, 0).rgb * params.bloom_intensity;
	if (params.use_lut_3d != 0)
		c = lut.SampleLevel(linear_clamp, linear_to_lut_uvw(c), 0).rgb;
	else
	{
		if (params.grade.has_grading != 0)
			c = apply_color_grading(c, params.grade);
		c = apply_tonemap(c, params.grade.tone_mapping);
		c = apply_strip(c, params.grade, external_strip, linear_clamp);
		if (params.grade.tone_mapping == 3 && params.output_linear == 0)
			c = saturate(c);
	}
	if (params.vignette_intensity > 0.0)
	{
		float r = length(uv - 0.5) * 1.41421356;
		c *= 1.0 - params.vignette_intensity * smoothstep(1.0 - params.vignette_smoothness, 1.0, r);
	}
	if (params.output_linear == 0)
	{
		c = linear_to_srgb(saturate(c));
		if (params.debanding != 0 && params.grade.tone_mapping != 3)
			c = saturate(c + (ign(position.xy) + ign(position.xy + float2(5.588238, 13.719231)) - 1.0) / 255.0);
	}
	return float4(min(c, 65504.0), isfinite(sampled.a) ? sampled.a : 0.0);
}
