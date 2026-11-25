import common;
import brdf;
import ddgi_common;
import ibl_common;
import pbr;

public struct IBLParams
{
	public float intensity;
	public float sin_rot;
	public float cos_rot;
	public float max_mip;
	public uint indirect_only;
	public float fade_start, max_roughness;
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

// Evaluate the diffuse environment and its inferred sky visibility at the same point.
public float3 indirect_environment(float3 world, float3 N, float3 incident, IBLParams sky, StructuredBuffer<float4> sh,
		DDGIVolumeData volume, DDGIResources probes, out float sky_visibility, out float3 probe_diffuse, bool need_sky_visibility = true)
{
	float blend = volume.active != 0 ? ddgi_volume_blend_weight(world, volume) : 0;
	if (blend > 0)
	{
		float3 bias = ddgi_surface_bias(N, incident, volume);
		float3 E = ddgi_get_volume_irradiance(world, bias, N, volume, probes) * (volume.intensity / PI);
		probe_diffuse = E * blend;
		if (sky.indirect_only != 0 || (blend >= 1.0 && !need_sky_visibility))
		{
			sky_visibility = 1;
			return E;
		}
		float3 sky_diffuse = evaluate_sh3(sh, inverse_sky_rotation(N, sky.sin_rot, sky.cos_rot)) * sky.intensity;
		float sky_lum = ddgi_luminance(sky_diffuse);
		float visibility = sky_lum > 1e-4 ? saturate(ddgi_luminance(E) / sky_lum) : 1;
		sky_visibility = lerp(1.0, visibility, blend);
		return lerp(sky_diffuse, E, blend);
	}
	sky_visibility = 1;
	probe_diffuse = 0;
	return sky.indirect_only != 0 ? float3(0) : evaluate_sh3(sh, inverse_sky_rotation(N, sky.sin_rot, sky.cos_rot)) * sky.intensity;
}

public float3 evaluate_ibl(ShadingData p, float material_ao, IBLParams sky, TextureCube<float4> prefilter_cubemap,
		SamplerState cube_sampler, StructuredBuffer<float4> sh, float3 world, DDGIVolumeData volume, DDGIResources probes, float4 rt_sample)
{
	float rt_blend = rt_sample.a * (1 - smoothstep(sky.fade_start, sky.max_roughness, p.perceptual_roughness));
	bool need_fallback = sky.indirect_only == 0 && rt_blend < 1.0 && sky.intensity > 0.0;
	float visibility;
	float3 probe_diffuse;
	float3 indirect = indirect_environment(world, p.N, -p.V, sky, sh, volume, probes, visibility, probe_diffuse, need_fallback);
	if (sky.indirect_only != 0)
		return probe_diffuse * p.diffuse * material_ao;
	float3 specular_dfg = lerp(p.dfg.xxx, p.dfg.yyy, p.f0);
	float3 specular_response = specular_dfg * p.compensation;
	float3 diffuse = indirect * p.diffuse * saturate(1 - specular_response) * material_ao;
	float3 fallback = float3(0);
	if (need_fallback)
	{
		float3 R_dom = normalize_or(lerp(reflect(-p.V, p.N), p.N, p.roughness * p.roughness), p.N);
		float3 R_rot = inverse_sky_rotation(R_dom, sky.sin_rot, sky.cos_rot);
		float specular_ao = saturate(pow(p.NoV + material_ao, exp2(-16.0 * p.roughness - 1.0)) - 1.0 + material_ao);
		float mip = sky.max_mip * p.perceptual_roughness * (2.0 - p.perceptual_roughness);
		fallback = prefilter_cubemap.SampleLevel(cube_sampler, R_rot, mip).rgb * sky.intensity * specular_ao * visibility;
	}
	return diffuse + lerp(fallback, rt_sample.rgb, rt_blend) * specular_response;
}
