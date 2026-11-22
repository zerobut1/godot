import common;
import gbuffer;
import brdf;

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
	float3 N = surface.normal_ws;
	float3 V = frame.camera_forward.w != 0.0 ? frame.camera_forward.xyz : normalize_or(frame.camera_position.xyz - world, N);
	float NoV = max(saturate(dot(N, V)), MIN_N_DOT_V);
	float perceptual_roughness = max(surface.roughness, MIN_PERCEPTUAL_ROUGHNESS);
	float roughness = perceptual_roughness * perceptual_roughness;
	float3 f0 = standard_f0(surface.base_color, surface.metallic, surface.specular);
	float3 diffuse = surface.base_color * (1.0 - surface.metallic);
	float3 compensation = energy_compensation(f0, dfg_lut.SampleLevel(dfg_sampler, float2(NoV, perceptual_roughness), 0.0).g);

	float hemisphere = lerp(0.025, 0.075, saturate(N.y * 0.5 + 0.5));
	float3 lighting = (diffuse + f0 * 0.25) * (hemisphere * surface.material_ao);
	for (uint i = 0; i < frame.light_count; ++i)
	{
		DirectionalLight light = frame.lights[i];
		float3 L = light.direction.xyz;
		float NoL = saturate(dot(N, L));
		if (NoL > 0.0)
		{
			float3 H = normalize_or(V + L, N);
			float NoH = saturate(dot(N, H));
			float LoH = saturate(dot(L, H));
			lighting += evaluate_brdf(diffuse, f0, roughness, NoV, NoL, NoH, LoH, compensation) * light.radiance.rgb * NoL;
		}
	}
	return float4(clamp(sanitize_finite(lighting), -65504.0, 65504.0), 0.0);
}
