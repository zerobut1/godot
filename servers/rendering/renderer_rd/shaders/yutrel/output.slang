import common;

[[vk::binding(0, 0)]] Texture2D<float4> scene_color;

struct OutputData
{
	uint output_linear;
	uint tone_mapping;
	float exposure;
};
[[vk::push_constant]] ConstantBuffer<OutputData> output_data;

float3 tonemap_agx(float3 color)
{
	// Troy Sobotka's AgX, with the minimal sigmoid fit and a punchy look.
	const float3x3 inset = float3x3(
		0.842479, 0.0784336, 0.0792237,
		0.0423282, 0.8784686, 0.0791661,
		0.0423757, 0.0784336, 0.8791429);
	const float3x3 outset = float3x3(
		1.1968790, -0.0980209, -0.0990297,
		-0.0528969, 1.1519031, -0.0989612,
		-0.0529716, -0.0980435, 1.1510737);
	const float min_ev = -12.47393;
	const float max_ev = 4.026069;
	float3 x = saturate((log2(max(mul(inset, color), exp2(min_ev))) - min_ev) / (max_ev - min_ev));
	float3 view = x * (x * (x * (x * (x * (15.5 * x - 40.14) + 31.96) - 6.868) + 0.4298) + 0.1191) - 0.00232;
	view = pow(saturate(view), 1.35);
	float luma = dot(view, float3(0.2126, 0.7152, 0.0722));
	view = lerp(luma, view, 1.4);
	return pow(saturate(mul(outset, view)), 2.2);
}

float3 tonemap_aces(float3 color)
{
	// ACES RRT/ODT fit in linear Rec.709, adapted from BakingLab.
	const float3x3 rgb_to_rrt = float3x3(
		0.59719, 0.35458, 0.04823,
		0.07600, 0.90834, 0.01566,
		0.02840, 0.13383, 0.83777);
	const float3x3 odt_to_rgb = float3x3(
		1.60475, -0.53108, -0.07367,
		-0.10208, 1.10813, -0.00605,
		-0.00327, -0.07276, 1.07602);
	float3 x = mul(rgb_to_rrt, color);
	float3 view = (x * (x + 0.0245786) - 0.000090537) / (x * (0.983729 * x + 0.4329510) + 0.238081);
	return saturate(mul(odt_to_rgb, view));
}

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	float4 sampled = scene_color.Load(int3(int2(position.xy), 0));
	float3 color = min(max(sanitize_finite(sampled.rgb), 0.0) * output_data.exposure, 65504.0);
	if (output_data.tone_mapping == 0)
	{
		color = tonemap_agx(color);
	}
	else if (output_data.tone_mapping == 1)
	{
		color = tonemap_aces(color);
	}
	else if (output_data.output_linear == 0)
	{
		color = saturate(color);
	}
	return float4(output_data.output_linear != 0 ? color : linear_to_srgb(color), isfinite(sampled.a) ? sampled.a : 0.0);
}
