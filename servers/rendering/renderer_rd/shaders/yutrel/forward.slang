import common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] StructuredBuffer<InstanceData> instances;

struct DrawData
{
	uint draw_id;
};
[[vk::push_constant]] ConstantBuffer<DrawData> draw;

struct VertexOutput
{
	float4 position : SV_Position;
	[[vk::location(0)]] float3 normal : NORMAL0;
	[[vk::location(1)]] nointerpolation uint layer_mask : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main([[vk::location(0)]] float4 position : POSITION0, [[vk::location(1)]] float2 normal : NORMAL0)
{
	InstanceData instance = instances[draw.draw_id];
	float3 world = mul_3x4(instance.model, float4(unpack_position(position, instance), 1.0));
	VertexOutput output;
	output.position = mul_4x4(frame.view_projection, float4(world, 1.0));
	output.normal = normalize(mul_normal_3x3(instance.normal, unpack_normal(normal, position.w, instance.flags)));
	output.layer_mask = instance.layer_mask;
	return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0
{
	float lambert = max(dot(normalize(input.normal), frame.light_direction.xyz), 0.0);
	float3 direct = (input.layer_mask & frame.light_cull_mask) != 0 ? frame.light_radiance * lambert : float3(0.0);
	return float4(0.6 * (0.05 + direct), 1.0);
}
