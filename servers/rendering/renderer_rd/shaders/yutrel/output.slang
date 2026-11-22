import common;
import output_common;

[[vk::binding(0, 0)]] Texture2D<float4> scene_color;

[[vk::push_constant]] ConstantBuffer<OutputData> output_data;

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

[shader("fragment")]
float4 fragment_main(float4 position : SV_Position) : SV_Target0
{
	float4 sampled = scene_color.Load(int3(int2(position.xy), 0));
	return float4(output_transform(sampled.rgb, output_data), isfinite(sampled.a) ? sampled.a : 0.0);
}
