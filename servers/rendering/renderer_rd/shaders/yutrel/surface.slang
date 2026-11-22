import common;
import gbuffer;

[[vk::binding(0, 0)]] public ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] StructuredBuffer<InstanceData> instances;
[[vk::binding(2, 0)]] StructuredBuffer<MaterialData> materials;

[[vk::binding(0, 1)]] Texture2D<float4> albedo_texture;
[[vk::binding(1, 1)]] Texture2D<float4> normal_texture;
[[vk::binding(2, 1)]] Texture2D<float4> roughness_texture;
[[vk::binding(3, 1)]] Texture2D<float4> metallic_texture;
[[vk::binding(4, 1)]] Texture2D<float4> ao_texture;
[[vk::binding(5, 1)]] Texture2D<float4> emissive_texture;
[[vk::binding(6, 1)]] SamplerState material_sampler;

public struct VertexInput
{
	[[vk::location(0)]] public float4 position : POSITION0;
	[[vk::location(1)]] public float4 axis_tangent : NORMAL0;
	[[vk::location(3)]] public float4 color : COLOR0;
	[[vk::location(4)]] public float2 uv : TEXCOORD0;
};

public struct VertexOutput
{
	public float4 position : SV_Position;
	[[vk::location(0)]] public float3 normal : NORMAL0;
	[[vk::location(1)]] public float3 tangent : TANGENT0;
	[[vk::location(2)]] public float3 binormal : BINORMAL0;
	[[vk::location(3)]] public float4 color : COLOR0;
	[[vk::location(4)]] public float2 uv : TEXCOORD0;
	[[vk::location(5)]] public nointerpolation uint material_id : TEXCOORD1;
};

public VertexOutput transform_vertex(VertexInput input, uint draw_id)
{
	InstanceData instance = instances[draw_id];
	MaterialData material = materials[instance.material_id];
	float3 world = mul_3x4(instance.model, float4(unpack_position(input.position, instance), 1.0));
	float3 normal, tangent, binormal;
	unpack_tbn(input.axis_tangent, input.position.w, instance.flags, normal, tangent, binormal);

	VertexOutput output;
	output.position = mul_4x4(frame.view_projection, float4(world, 1.0));
	output.normal = normalize(mul_normal_3x3(instance.normal, normal));
	output.tangent = mul_normal_3x3(instance.model, tangent);
	output.binormal = mul_normal_3x3(instance.model, binormal);
	output.color = (instance.flags & INSTANCE_HAS_COLOR) != 0 ? input.color : float4(1.0);
	if ((material.flags & SRGB_VERTEX_COLOR) != 0)
	{
		output.color.rgb = srgb_to_linear(output.color.rgb);
	}
	output.uv = unpack_uv(input.uv, instance) * material.uv_transform.xy + material.uv_transform.zw;
	output.material_id = instance.material_id;
	return output;
}

public GBufferData sample_surface(VertexOutput input, bool is_front_face, bool transparent, out float alpha, out float3 emissive)
{
	MaterialData material = materials[input.material_id];
	float3 geometric_normal = normalize(input.normal);

	float4 base = material.albedo * albedo_texture.Sample(material_sampler, input.uv);
	if ((material.flags & VERTEX_COLOR) != 0)
	{
		base *= input.color;
	}
	alpha = saturate(base.a);
	if ((transparent && alpha <= 0.0) || ((material.flags & ALPHA_SCISSOR) != 0 && base.a < material.alpha_scissor_threshold))
	{
		discard;
	}

	float3 normal = geometric_normal;
	if ((material.flags & HAS_NORMAL_MAP) != 0 && dot(input.tangent, input.tangent) > 1e-12)
	{
		float3 tangent = normalize_or(input.tangent - normal * dot(normal, input.tangent), float3(0.0));
		float3 binormal = cross(normal, tangent);
		binormal *= dot(binormal, input.binormal) < 0.0 ? -1.0 : 1.0;
		float2 xy = normal_texture.Sample(material_sampler, input.uv).rg * 2.0 - 1.0;
		// Reconstruct Z for RG/BC5 and RGB normal maps alike.
		float z = sqrt(saturate(1.0 - dot(xy, xy)));
		xy *= material.normal_scale;
		normal = normalize_or(tangent * xy.x + binormal * xy.y + normal * z, normal);
	}
	if ((material.flags & DOUBLE_SIDED) != 0 && !is_front_face)
	{
		normal = -normal;
	}

	float roughness = saturate(material.roughness * dot(roughness_texture.Sample(material_sampler, input.uv), material.roughness_channel));
	emissive = material.emission;
	if ((material.flags & HAS_EMISSION_TEXTURE) != 0)
	{
		float3 texel = emissive_texture.Sample(material_sampler, input.uv).rgb;
		emissive = (material.flags & EMISSION_OP_MULTIPLY) != 0 ? emissive * texel : emissive + texel;
	}

	GBufferData surface;
	surface.base_color = saturate(base.rgb);
	surface.normal_ws = normal;
	surface.roughness = roughness;
	surface.metallic = saturate(material.metallic * dot(metallic_texture.Sample(material_sampler, input.uv), material.metallic_channel));
	surface.specular = saturate(material.specular);
	surface.material_ao = saturate(dot(ao_texture.Sample(material_sampler, input.uv), material.ao_channel));
	surface.shading_model_id = (material.flags & UNLIT) != 0 ? SHADING_UNLIT : SHADING_STANDARD;
	emissive *= material.emission_energy;
	if (surface.shading_model_id == SHADING_UNLIT)
	{
		emissive += surface.base_color;
	}
	return surface;
}
