import common;
import ibl_common;
import pbr;

public struct IBLParams
{
	public float intensity;
	public float sin_rot;
	public float cos_rot;
	public float max_mip;
};

float3 evaluate_sh3(StructuredBuffer<float4> sh, float3 N)
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

public float3 evaluate_ibl(ShadingData p, float material_ao, IBLParams sky, TextureCube<float4> prefilter_cubemap,
		SamplerState cube_sampler, StructuredBuffer<float4> sh)
{
	float3 N_rot = inverse_sky_rotation(p.N, sky.sin_rot, sky.cos_rot);
	float3 R_dom = normalize_or(lerp(reflect(-p.V, p.N), p.N, p.roughness * p.roughness), p.N);
	float3 R_rot = inverse_sky_rotation(R_dom, sky.sin_rot, sky.cos_rot);
	float3 specular_dfg = lerp(p.dfg.xxx, p.dfg.yyy, p.f0);
	float3 specular_response = specular_dfg * p.compensation;
	float specular_ao = saturate(pow(p.NoV + material_ao, exp2(-16.0 * p.roughness - 1.0)) - 1.0 + material_ao);
	float mip = sky.max_mip * p.perceptual_roughness * (2.0 - p.perceptual_roughness);
	float3 specular = prefilter_cubemap.SampleLevel(cube_sampler, R_rot, mip).rgb * specular_response * specular_ao;
	float3 diffuse = evaluate_sh3(sh, N_rot) * p.diffuse * saturate(1.0 - specular_response) * material_ao;
	return (diffuse + specular) * sky.intensity;
}
