import common;

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

// Heitz (2018): sample the projected disk in the stretched view frame.
public float3 sample_vndf(float3 v, float alpha, float2 u)
{
	float3 vh = normalize(float3(alpha * v.xy, v.z));
	float3 t = normalize_or(float3(-vh.y, vh.x, 0), float3(1, 0, 0));
	float angle = 2 * PI * u.y;
	float2 disk = sqrt(u.x) * float2(cos(angle), sin(angle));
	disk.y = lerp(sqrt(saturate(1 - disk.x * disk.x)), disk.y, 0.5 * (1 + vh.z));
	float3 h = t * disk.x + cross(vh, t) * disk.y + vh * sqrt(saturate(1 - dot(disk, disk)));
	return normalize(float3(alpha * h.xy, max(h.z, 0.0)));
}
