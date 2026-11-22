import common;
import ibl_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] TextureCube<float4> source_cubemap;
[[vk::binding(2, 0)]] SamplerState cube_sampler;

struct SkyboxParams
{
	float intensity;
	float sin_rot;
	float cos_rot;
};
[[vk::push_constant]] ConstantBuffer<SkyboxParams> params;

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

[shader("fragment")]
[earlydepthstencil]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	float3 ray = -frame.camera_forward.xyz;
	if (frame.camera_forward.w == 0.0)
	{
		float3 world = reconstruct_world_position(position.xy, 0.5, frame);
		ray = normalize_or(world - frame.camera_position.xyz, ray);
	}
	ray = inverse_sky_rotation(ray, params.sin_rot, params.cos_rot);
	float3 color = source_cubemap.SampleLevel(cube_sampler, ray, 0.0).rgb * params.intensity;
	return float4(clamp(sanitize_finite(color), 0.0, 65504.0), 1.0);
}
