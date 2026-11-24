import common;

public struct GBufferTargets
{
	public float4 scene_color : SV_Target0;
	public float4 gbuffer_a : SV_Target1;
	public float4 gbuffer_b : SV_Target2;
	public float4 gbuffer_c : SV_Target3;
};

public GBufferTargets encode_gbuffer(SurfaceData surface)
{
	GBufferTargets result;
	result.scene_color = float4(clamp(sanitize_finite(surface.self_illumination()), 0.0, 65504.0), 1.0);
	result.gbuffer_a = float4(surface.base_color, float(surface.shading_model_id) / 255.0);
	result.gbuffer_b = float4(surface.normal_ws * 0.5 + 0.5, 0.0);
	result.gbuffer_c = float4(surface.roughness, surface.metallic, surface.specular, surface.material_ao);
	return result;
}

public SurfaceData decode_gbuffer(float4 a, float4 b, float4 c)
{
	SurfaceData surface = {};
	surface.alpha = 1.0;
	surface.base_color = a.rgb;
	surface.normal_ws = normalize_or(b.rgb * 2.0 - 1.0, float3(0.0, 0.0, 1.0));
	surface.roughness = c.r;
	surface.metallic = c.g;
	surface.specular = c.b;
	surface.material_ao = c.a;
	surface.shading_model_id = uint(round(a.a * 255.0));
	return surface;
}
