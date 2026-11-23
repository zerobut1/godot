import pbr;
import common;
import gbuffer;
import ibl_lighting;
import ddgi_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(3, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(4, 0)]] Texture2D<float> scene_depth;
[[vk::binding(5, 0)]] Texture2D<float2> dfg_lut;
[[vk::binding(6, 0)]] SamplerState linear_clamp;
[[vk::binding(7, 0)]] TextureCube<float4> prefilter_cubemap;
[[vk::binding(8, 0)]] SamplerState cube_sampler;
[[vk::binding(9, 0)]] StructuredBuffer<float4> sh;
[[vk::binding(10, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(11, 0)]] Texture2DArray<float4> probe_irradiance;
[[vk::binding(12, 0)]] Texture2DArray<float2> probe_distance;
[[vk::binding(13, 0)]] Texture2DArray<float4> probe_data;
[[vk::binding(14, 0)]] Texture2D<float> ao_texture;

[[vk::binding(15, 0)]] Texture2D<float4> rt_reflection_texture;

[[vk::push_constant]] ConstantBuffer<IBLParams> params;

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
	ShadingData p = prepare_shading(surface, frame, world, dfg_lut, linear_clamp);
	DDGIResources probes = { probe_irradiance, probe_distance, probe_data, linear_clamp };
	float ao = surface.material_ao * load_viewport_ao(ao_texture, pixel.xy - int2(frame.viewport_origin));
	float3 color = evaluate_ibl(p, ao, params, prefilter_cubemap, cube_sampler, sh,
			world, volume, probes, load_viewport_reflection(rt_reflection_texture, pixel.xy - int2(frame.viewport_origin)));
	return float4(clamp(sanitize_finite(color), 0.0, 65504.0), 0.0);
}
