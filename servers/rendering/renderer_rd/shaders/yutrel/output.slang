import common;

[[vk::binding(0, 0)]] Texture2D<float4> scene_color;

struct OutputData
{
	uint output_linear;
};
[[vk::push_constant]] ConstantBuffer<OutputData> output_data;

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	float2 corner = float2(vertex_id & 1, vertex_id >> 1);
	return float4(corner * 4.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	// SV_Position is in attachment coordinates, including an offset render region.
	float4 sampled = scene_color.Load(int3(int2(position.xy), 0));
	float3 color = max(sampled.rgb, 0.0);
	color /= 1.0 + color; // Reinhard, exposure 1.
	return float4(output_data.output_linear != 0 ? color : linear_to_srgb(color), sampled.a);
}
