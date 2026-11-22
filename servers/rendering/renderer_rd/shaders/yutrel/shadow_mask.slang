import common;
import csm;
import gbuffer;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(4, 0)]] Texture2D<float> scene_depth;
[[vk::binding(5, 0)]] ConstantBuffer<CSMShadowData> csm;
[[vk::binding(6, 0)]] Texture2DArray<float> csm_shadow_array;
[[vk::binding(7, 0)]] SamplerComparisonState comparison_sampler;
struct Display { uint cascades; };
[[vk::push_constant]] ConstantBuffer<Display> display;

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
	if (depth <= 0.0)
		return display.cascades != 0 ? float4(0, 0, 0, 1) : float4(1);
	GBufferData surface = decode_gbuffer(gbuffer_a.Load(pixel), gbuffer_b.Load(pixel), gbuffer_c.Load(pixel));
	float3 world = reconstruct_world_position(position.xy, depth, frame);
	float distance = dot(frame.camera_position.xyz - world, frame.camera_forward.xyz);
	CascadeSelection cascade = select_cascade(csm, distance);

	if (display.cascades != 0)
	{
		static const float3 colors[4] = { float3(1, 0.12, 0.1), float3(0.1, 1, 0.18), float3(0.12, 0.24, 1), float3(1, 0.82, 0.10) };
		float shade = 0.2 + 0.45 * saturate(dot(surface.normal_ws, normalize(float3(0.3, 1, 0.4))));
		return float4(shade * lerp(float3(0.23), lerp(colors[cascade.index], colors[cascade.next], cascade.blend), cascade.strength * 0.8), 1);
	}
	return float4(evaluate_csm_shadow(csm, csm_shadow_array, comparison_sampler, cascade, world, surface.normal_ws, float2(pixel.xy)));
}
