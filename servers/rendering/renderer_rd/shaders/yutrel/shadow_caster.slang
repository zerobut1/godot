import common;

[[vk::constant_id(0)]] const bool scissor = false;
[[vk::binding(1, 0)]] StructuredBuffer<InstanceData> instances;
[[vk::binding(2, 0)]] StructuredBuffer<MaterialData> materials;
[[vk::binding(0, 1)]] Texture2D<float4> albedo_texture;
[[vk::binding(1, 1)]] SamplerState material_sampler;

struct ShadowDraw
{
	float4 vp[4];
	uint draw_id;
};
[[vk::push_constant]] ConstantBuffer<ShadowDraw> draw;

struct VertexOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
	float alpha : TEXCOORD1;
	nointerpolation float threshold : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertex_main([[vk::location(0)]] float4 position : POSITION0,
	[[vk::location(3)]] float4 color : COLOR0, [[vk::location(4)]] float2 uv : TEXCOORD0)
{
	InstanceData instance = instances[draw.draw_id];
	float3 world = mul_3x4(instance.model, float4(unpack_position(position, instance), 1.0));
	VertexOutput output = {};
	output.position = mul_4x4(draw.vp, float4(world, 1.0));
	// Reverse-Z near plane is z=w. Preserve the silhouette of casters beyond it.
	output.position.z = min(output.position.z, output.position.w);
	if (scissor)
	{
		MaterialData material = materials[instance.material_id];
		output.uv = unpack_uv(uv, instance) * material.uv_transform.xy + material.uv_transform.zw;
		output.alpha = material.albedo.a;
		if ((instance.flags & INSTANCE_HAS_COLOR) != 0 && (material.flags & VERTEX_COLOR) != 0)
			output.alpha *= color.a;
		output.threshold = material.alpha_scissor_threshold;
	}
	return output;
}

[shader("fragment")]
void fragment_main(VertexOutput input)
{
	if (scissor)
	{
		if (input.alpha * albedo_texture.Sample(material_sampler, input.uv).a < input.threshold)
			discard;
	}
}
