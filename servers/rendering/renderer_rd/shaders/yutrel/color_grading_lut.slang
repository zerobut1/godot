import color_grading_common;

[[vk::binding(0, 0)]] ConstantBuffer<OutputUniforms> params;
[[vk::binding(1, 0)]] Texture2D<float4> strip;
[[vk::binding(2, 0)]] SamplerState linear_clamp;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> lut;

[shader("compute")]
[numthreads(4, 4, 4)]
void compute_main(uint3 cell : SV_DispatchThreadID)
{
	float3 c = lut_u_to_linear(float3(cell) / (LUT_SIZE - 1.0));
	if (params.grade.has_grading != 0)
		c = apply_color_grading(c, params.grade);
	c = apply_tonemap(c, params.grade.tone_mapping);
	lut[cell] = float4(apply_strip(c, params.grade, strip, linear_clamp), 1.0);
}
