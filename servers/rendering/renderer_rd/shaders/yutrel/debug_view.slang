import common;
import gbuffer;

[[vk::binding(0, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(3, 0)]] Texture2D<float> scene_depth;
[[vk::binding(4, 0)]] Texture2D<float4> scene_color;

[[vk::binding(5, 0)]] Texture2D<float> shadow_mask;
[[vk::binding(6, 0)]] Texture2D<float> ao_texture;

[[vk::binding(7, 0)]] Texture2D<float4> rt_reflection_texture;

struct DebugData
{
	int2 viewport_origin;
	uint output_linear;
	uint mode;
	uint has_primary_shadow;
};
[[vk::push_constant]] ConstantBuffer<DebugData> debug_data;

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
	if (depth <= 0.0 && debug_data.mode != 8 && debug_data.mode != 13)
	{
		return float4(0.0, 0.0, 0.0, 1.0);
	}
	float4 a = gbuffer_a.Load(pixel);
	float4 b = gbuffer_b.Load(pixel);
	GBufferData surface = decode_gbuffer(a, b, gbuffer_c.Load(pixel));
	float3 color = float3(0.0);
	bool display_encoded = false;
	// DDGI Indirect (11) and DDGI Probes (12) use the HDR output pass.
	switch (debug_data.mode)
	{
		case 1: color = surface.base_color; break;
		case 2: color = b.rgb; display_encoded = true; break;
		case 3: color = surface.roughness; display_encoded = true; break;
		case 4: color = surface.metallic; display_encoded = true; break;
		case 5: color = surface.material_ao; display_encoded = true; break;
		case 6:
			color = max(sanitize_finite(scene_color.Load(pixel).rgb), 0.0);
			color /= 1.0 + color;
			break;
		case 8:
			color = debug_data.has_primary_shadow != 0 ? shadow_mask.Load(pixel) : 1.0;
			display_encoded = true;
			break;
		case 9:
			color = debug_data.has_primary_shadow != 0 ? scene_color.Load(pixel).rgb : float3(0.0);
			break;
		case 13:
			color = load_viewport_ao(ao_texture, pixel.xy - debug_data.viewport_origin);
			display_encoded = true;
			break;
		case 14:
		{
			float4 rt = load_viewport_reflection(rt_reflection_texture, pixel.xy - debug_data.viewport_origin);
			color = rt.a > 0 ? rt.rgb / (1 + rt.rgb) : 0;
			break;
		}
		case 7:
			// A logarithmic display curve reveals distant reverse-Z geometry.
			color = log2(1.0 + 4095.0 * saturate(depth)) / 12.0;
			display_encoded = true;
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
