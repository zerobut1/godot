import brdf;
import common;
import ibl_common;

[[vk::binding(0, 0)]] Texture2D<float4> panorama;
[[vk::binding(1, 0)]] SamplerState panorama_sampler;
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> cube_face;

struct BakeParams
{
	float3 color;
	uint use_panorama;
	uint face;
	uint size;
};
[[vk::push_constant]] ConstantBuffer<BakeParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 id : SV_DispatchThreadID)
{
	float3 color = params.color;
	if (params.use_panorama != 0)
	{
		float2 face_uv = (float2(id.xy) + 0.5) * (2.0 / params.size) - 1.0;
		float3 dir = normalize(cube_direction(params.face, face_uv));
		// Longitude wraps at +Z. The top row faces +Y and the center faces -Z.
		float2 uv = float2(atan2(dir.x, -dir.z) / (2.0 * PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / PI);
		color = panorama.SampleLevel(panorama_sampler, uv, 0.0).rgb;
	}
	cube_face[id.xy] = float4(clamp(sanitize_finite(color), 0.0, 65504.0), 1.0);
}
