import brdf;
import ibl_common;

[[vk::binding(0, 0)]] TextureCube<float4> source_cubemap;
[[vk::binding(1, 0)]] SamplerState cube_sampler;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> destination_face;

struct PrefilterParams
{
	uint face;
	uint size;
	float mip_fraction;
	float source_texel_solid_angle;
};
[[vk::push_constant]] ConstantBuffer<PrefilterParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= params.size))
	{
		return;
	}

	float2 uv = (float2(id.xy) + 0.5) * (2.0 / params.size) - 1.0;
	float3 N = normalize(cube_direction(params.face, uv));
	if (params.mip_fraction == 0.0)
	{
		destination_face[id.xy] = source_cubemap.SampleLevel(cube_sampler, N, 0.0);
		return;
	}

	float perceptual_roughness = 1.0 - sqrt(1.0 - params.mip_fraction);
	float alpha = perceptual_roughness * perceptual_roughness;
	float a2 = alpha * alpha;
	float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	float3 tangent = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);
	const uint sample_count = 128;
	float3 sum = float3(0.0);
	float weight = 0.0;

	for (uint i = 0; i < sample_count; ++i)
	{
		float2 u = float2(float(i) / sample_count, float(reversebits(i)) * 2.3283064365386963e-10);
		float phi = 2.0 * PI * u.x;
		float NoH = sqrt((1.0 - u.y) / ((1.0 - u.y) + a2 * u.y));
		float sin_theta = sqrt(max(1.0 - NoH * NoH, 0.0));
		float3 H = (tangent * cos(phi) + bitangent * sin(phi)) * sin_theta + N * NoH;
		float3 L = 2.0 * NoH * H - N;
		float NoL = saturate(dot(N, L));
		if (NoL > 0.0)
		{
			// V = N, so the half-vector to reflection Jacobian gives pdf = D / 4.
			float denominator = (1.0 - NoH * NoH) + a2 * NoH * NoH;
			float pdf = a2 / (4.0 * PI * denominator * denominator);
			float sample_solid_angle = rcp(sample_count * pdf);
			float lod = max(0.0, 0.5 * log2(sample_solid_angle / params.source_texel_solid_angle));
			sum += source_cubemap.SampleLevel(cube_sampler, L, lod).rgb * NoL;
			weight += NoL;
		}
	}
	destination_face[id.xy] = float4(sum / weight, 1.0);
}
