import brdf;

[[vk::binding(0, 0)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> dfg_lut;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 thread_id : SV_DispatchThreadID)
{
	const uint size = 64;
	const uint sample_count = 1024;
	float2 uv = (float2(thread_id.xy) + 0.5) / float(size);
	float NoV = uv.x;
	float perceptual_roughness = max(uv.y, MIN_PERCEPTUAL_ROUGHNESS);
	float roughness = perceptual_roughness * perceptual_roughness;
	float a2 = roughness * roughness;
	float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);

	float2 integral = float2(0.0);
	for (uint i = 0; i < sample_count; ++i)
	{
		float2 u = float2(float(i) / sample_count, float(reversebits(i)) * 2.3283064365386963e-10);
		float phi = 2.0 * PI * u.x;
		float NoH = sqrt((1.0 - u.y) / (1.0 + (a2 - 1.0) * u.y));
		float sin_theta = sqrt(1.0 - NoH * NoH);
		float3 H = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, NoH);
		float VoH = saturate(dot(V, H));
		float NoL = saturate(2.0 * VoH * NoH - NoV);
		if (NoL > 0.0)
		{
			float visibility = 4.0 * V_SmithGGXCorrelated(roughness, NoV, NoL) * NoL * VoH / NoH;
			// Filament multi-scatter endpoints. lerp(R, G, F0) evaluates the split sum.
			integral += visibility * float2(pow5(1.0 - VoH), 1.0);
		}
	}
	dfg_lut[thread_id.xy] = saturate(integral / sample_count);
}
