import common;
import gbuffer;
import pbr;
import local_lights;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(4, 0)]] Texture2D<float> scene_depth;
[[vk::binding(5, 0)]] Texture2D<float2> dfg_lut;
[[vk::binding(6, 0)]] SamplerState dfg_sampler;

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

#ifdef LOCAL_LIGHT_RT
	float3 Ng = geometric_normal(scene_depth, frame, pixel.xy, int2(round(1 / frame.inv_viewport_size)), depth, world, p.N);
	LocalLightRay shadows = { offset_ray_origin(world, Ng), Ng, 0.002 };
#else
	UnshadowedLocalLights shadows;
#endif
	float3 lighting = shade_cluster(position.xy, world, p, shadows);
	return float4(clamp(sanitize_finite(lighting), -65504.0, 65504.0), 0.0);
}
