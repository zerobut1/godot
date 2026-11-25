import common;
import surface_evaluation;

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
	public precise float4 position : SV_Position;
	[[vk::location(0)]] public float3 normal : NORMAL0;
	[[vk::location(1)]] public float3 tangent : TANGENT0;
	[[vk::location(2)]] public float3 binormal : BINORMAL0;
	[[vk::location(3)]] public float4 color : COLOR0;
	[[vk::location(4)]] public float2 uv : TEXCOORD0;
	[[vk::location(5)]] public nointerpolation uint material_id : TEXCOORD1;
	[[vk::location(6)]] public float4 curr_clip : TEXCOORD2;
	[[vk::location(7)]] public float4 prev_clip : TEXCOORD3;
};

public VertexOutput transform_vertex(VertexInput input, uint draw_id, bool scissor = false)
{
	InstanceData instance = instances[draw_id];
	MaterialData material = materials[instance.material_id];
	float3 local = unpack_position(input.position, instance);
	float3 world = mul_3x4(instance.model, float4(local, 1.0));
	float3 normal, tangent, binormal;
	unpack_tbn(input.axis_tangent, input.position.w, instance.flags, normal, tangent, binormal);

	VertexOutput output;
	output.position = clip_position_from_local(local, instance, frame);
	output.curr_clip = mul_4x4(frame.unjittered_view_projection, float4(world, 1));
	output.prev_clip = mul_4x4(frame.prev_unjittered_view_projection, float4(mul_3x4(instance.prev_model, float4(local, 1.0)), 1));
	output.normal = normalize(mul_normal_3x3(instance.normal, normal));
	output.tangent = mul_normal_3x3(instance.model, tangent);
	output.binormal = mul_normal_3x3(instance.model, binormal);
	output.color = linear_vertex_color(material, (instance.flags & INSTANCE_HAS_COLOR) != 0 ? input.color : float4(1.0));
	if (scissor)
		output.color.a = scissor_vertex_alpha(input.color.a, instance, material);
	output.uv = unpack_uv(input.uv, instance) * material.uv_transform.xy + material.uv_transform.zw;
	output.material_id = instance.material_id;
	return output;
}

struct RasterSurfaceMaps : ISurfaceMaps
{
	float2 uv;

	float4 sample_texture(SurfaceTexture slot)
	{
		switch (slot)
		{
		case SurfaceTexture::Albedo: return albedo_texture.Sample(material_sampler, uv);
		case SurfaceTexture::Normal: return normal_texture.Sample(material_sampler, uv);
		case SurfaceTexture::Roughness: return roughness_texture.Sample(material_sampler, uv);
		case SurfaceTexture::Metallic: return metallic_texture.Sample(material_sampler, uv);
		case SurfaceTexture::Occlusion: return ao_texture.Sample(material_sampler, uv);
		default: return emissive_texture.Sample(material_sampler, uv);
		}
	}
};

public uint material_blend_mode(uint id) { return materials[id].blend_mode; }

public SurfaceData sample_surface(VertexOutput input, bool is_front_face, bool transparent, bool scissor = false)
{
	MaterialData material = materials[input.material_id];
	RasterSurfaceMaps maps = { input.uv };
	float4 albedo = maps.sample_texture(SurfaceTexture::Albedo);
	float4 base = evaluate_base(material, albedo, input.color);
	if (scissor)
	{
		// Use the same interpolated alpha and multiplication order as the depth prepass.
		base.a = input.color.a * albedo.a;
		if (base.a < material.alpha_scissor_threshold)
			discard;
	}
	if (transparent && (base.a <= 0.0 || ((material.flags & ALPHA_SCISSOR) != 0 && base.a < material.alpha_scissor_threshold)))
		discard;
	return evaluate_surface(material, maps, base, normalize(input.normal), input.tangent, input.binormal, is_front_face);
}
