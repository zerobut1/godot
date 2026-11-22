import common;
import gbuffer;
import pbr;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(4, 0)]] Texture2D<float> scene_depth;
[[vk::binding(5, 0)]] Texture2D<float2> dfg_lut;
[[vk::binding(6, 0)]] SamplerState dfg_sampler;

[[vk::binding(7, 0)]] Texture2D<float> shadow_mask;

struct DirectionalLightParams
{
	float4 direction;
	float3 radiance;
	uint has_shadow;
};
[[vk::push_constant]] ConstantBuffer<DirectionalLightParams> light;

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
	float3 world = reconstruct_world_position(position.xy, depth, frame);
	ShadingData p = prepare_shading(surface, frame, world, dfg_lut, dfg_sampler);

	float3 lighting = float3(0.0);
	float3 L = light.direction.xyz;
	float NoL = saturate(dot(p.N, L));
	if (NoL > 0.0)
	{
		float visibility = light.has_shadow != 0 ? shadow_mask.Load(pixel) : 1.0;
		lighting = direct_brdf(p, L, NoL) * light.radiance * (NoL * visibility);
	}
	return float4(clamp(sanitize_finite(lighting), -65504.0, 65504.0), 0.0);
}
