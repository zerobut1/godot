import common;
import rt_scene;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;

struct RTDebugData
{
	uint output_linear, mode, has_tlas, padding;
};
[[vk::push_constant]] ConstantBuffer<RTDebugData> debug_data;

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	if (debug_data.has_tlas == 0)
		return float4(0, 0, 0, 1);
	float3 origin = reconstruct_world_position(position.xy, 1.0, frame);
	float3 dir = frame.camera_forward.w != 0 ? -frame.camera_forward.xyz : normalize(origin - frame.camera_position.xyz);
	RTHit hit;
	if (!trace_surface(origin, dir, 0.0, 1e20, hit, debug_data.mode == 21, false, float2(0), false, false))
		return float4(0, 0, 0, 1);

	float3 color = 0;
	bool display_encoded = true;
	switch (debug_data.mode)
	{
		case 15: color = hit.surface.base_color; display_encoded = false; break;
		case 16: color = hit.surface.normal_ws * 0.5 + 0.5; break;
		case 17: color = hit.geometric_normal * 0.5 + 0.5; break;
		case 18: color = float3(hit.surface.roughness); break;
		case 19: color = float3(hit.surface.metallic); break;
		case 20:
			color = max(sanitize_finite(hit.surface.self_illumination()), 0.0);
			color /= 1.0 + color;
			display_encoded = false;
			break;
		case 21:
			if (hit.is_front_face)
				color = float3(0.15, 0.85, 0.25);
			else
				color = hit.is_double_sided ? float3(0.95, 0.80, 0.15) : float3(0.95, 0.15, 0.20);
			break;
	}
	if (display_encoded)
	{
		color = debug_data.output_linear != 0 ? srgb_to_linear(color) : color;
	}
	else if (debug_data.output_linear == 0)
	{
		color = linear_to_srgb(color);
	}
	return float4(color, 1.0);
}
