import brdf;
import common;
import gbuffer;
import ibl_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(4, 0)]] Texture2D<float> scene_depth;
[[vk::binding(5, 0)]] Texture2D<float2> dfg_lut;
[[vk::binding(6, 0)]] SamplerState linear_clamp;
[[vk::binding(7, 0)]] TextureCube<float4> prefilter_cubemap;
[[vk::binding(8, 0)]] SamplerState cube_sampler;
[[vk::binding(9, 0)]] StructuredBuffer<float4> sh;

struct IBLParams
{
	float intensity;
	float sin_rot;
	float cos_rot;
	float max_mip;
};
[[vk::push_constant]] ConstantBuffer<IBLParams> params;

float3 evaluate_sh3(float3 N)
{
	float basis[9];
	sh3_basis(N, basis);
	float3 irradiance = float3(0.0);
	for (uint i = 0; i < 9; ++i)
	{
		irradiance += sh[i].rgb * basis[i];
	}
	return max(irradiance, 0.0);
}

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	int3 pixel = int3(int2(position.xy), 0);
	float depth = scene_depth.Load(pixel);
	float4 a = gbuffer_a.Load(pixel);
	if (depth <= 0.0 || a.a == 0.0)
	{
		discard;
	}
	GBufferData surface = decode_gbuffer(a, gbuffer_b.Load(pixel), gbuffer_c.Load(pixel));
	float3 N = surface.normal_ws;
	float3 world = reconstruct_world_position(position.xy, depth, frame);
	float3 V = frame.camera_forward.w != 0.0 ? frame.camera_forward.xyz : normalize_or(frame.camera_position.xyz - world, N);
	float NoV = max(saturate(dot(N, V)), MIN_N_DOT_V);
	float perceptual_roughness = max(surface.roughness, MIN_PERCEPTUAL_ROUGHNESS);
	float roughness = perceptual_roughness * perceptual_roughness;
	float3 f0 = standard_f0(surface.base_color, surface.metallic, surface.specular);
	float3 diffuse_color = surface.base_color * (1.0 - surface.metallic);

	float3 N_rot = inverse_sky_rotation(N, params.sin_rot, params.cos_rot);
	float3 R_dom = normalize_or(lerp(reflect(-V, N), N, roughness * roughness), N);
	float3 R_rot = inverse_sky_rotation(R_dom, params.sin_rot, params.cos_rot);
	float2 dfg = dfg_lut.SampleLevel(linear_clamp, float2(NoV, perceptual_roughness), 0.0);
	float3 specular_dfg = lerp(dfg.xxx, dfg.yyy, f0);
	float3 specular_response = specular_dfg * energy_compensation(f0, dfg.g);
	float specular_ao = saturate(pow(NoV + surface.material_ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + surface.material_ao);
	float mip = params.max_mip * perceptual_roughness * (2.0 - perceptual_roughness);
	float3 specular = prefilter_cubemap.SampleLevel(cube_sampler, R_rot, mip).rgb * specular_response * specular_ao;
	float3 diffuse = evaluate_sh3(N_rot) * diffuse_color * saturate(1.0 - specular_response) * surface.material_ao;
	return float4(clamp(sanitize_finite((diffuse + specular) * params.intensity), 0.0, 65504.0), 0.0);
}
