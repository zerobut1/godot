public static const float PI = 3.141592653589793;
public static const float MIN_N_DOT_V = 1e-4;
public static const float MIN_PERCEPTUAL_ROUGHNESS = 0.045;

public float pow5(float value)
{
	float squared = value * value;
	return squared * squared * value;
}

public float D_GGX(float roughness, float NoH)
{
	float a = NoH * roughness;
	float k = min(roughness / (1.0 - NoH * NoH + a * a), 453.5);
	return k * k / PI;
}

public float V_SmithGGXCorrelated(float roughness, float NoV, float NoL)
{
	float a2 = roughness * roughness;
	float gv = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
	float gl = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
	return 0.5 / max(gv + gl, 1e-5);
}

public float3 F_Schlick(float3 f0, float f90, float LoH)
{
	return lerp(f0, f90, pow5(1.0 - LoH));
}

public float Fd_Lambert()
{
	return 1.0 / PI;
}

public float3 standard_f0(float3 base_color, float metallic, float specular)
{
	return lerp(0.08 * specular, base_color, metallic);
}

public float3 energy_compensation(float3 f0, float dfg_visibility)
{
	return 1.0 + f0 * (rcp(max(dfg_visibility, 1e-4)) - 1.0);
}

public float3 evaluate_brdf(float3 diffuse, float3 f0, float roughness, float NoV, float NoL, float NoH, float LoH, float3 compensation)
{
	float f90 = saturate(dot(f0, float3(16.5)));
	float3 specular = D_GGX(roughness, NoH) * V_SmithGGXCorrelated(roughness, NoV, NoL) * F_Schlick(f0, f90, LoH);
	return diffuse * Fd_Lambert() + specular * compensation;
}
