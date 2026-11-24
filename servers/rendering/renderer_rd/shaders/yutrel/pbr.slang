import brdf;
import common;

public struct ShadingData
{
	public float3 N;
	public float3 V;
	public float NoV;
	public float perceptual_roughness;
	public float roughness;
	public float3 f0;
	public float3 diffuse;
	public float2 dfg;
	public float3 compensation;
};

public ShadingData prepare_shading(SurfaceData surface, FrameData frame, float3 world, Texture2D<float2> dfg_lut, SamplerState dfg_sampler)
{
	float3 N = surface.normal_ws;
	float3 V = frame.camera_forward.w != 0.0 ? frame.camera_forward.xyz : normalize_or(frame.camera_position.xyz - world, N);
	float NoV = max(saturate(dot(N, V)), MIN_N_DOT_V);
	float perceptual_roughness = max(surface.roughness, MIN_PERCEPTUAL_ROUGHNESS);
	float roughness = perceptual_roughness * perceptual_roughness;
	float3 f0 = standard_f0(surface.base_color, surface.metallic, surface.specular);
	float3 diffuse = surface.base_color * (1.0 - surface.metallic);
	float2 dfg = dfg_lut.SampleLevel(dfg_sampler, float2(NoV, perceptual_roughness), 0.0);
	float3 compensation = energy_compensation(f0, dfg.g);
	return { N, V, NoV, perceptual_roughness, roughness, f0, diffuse, dfg, compensation };
}

public float3 direct_brdf(ShadingData p, float3 L, float NoL)
{
	float3 H = normalize_or(p.V + L, p.N);
	float NoH = saturate(dot(p.N, H));
	float LoH = saturate(dot(L, H));
	return evaluate_brdf(p.diffuse, p.f0, p.roughness, p.NoV, NoL, NoH, LoH, p.compensation);
}
