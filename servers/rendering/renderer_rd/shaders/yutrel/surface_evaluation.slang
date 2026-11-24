import common;

// Slots follow YutrelMaterialBridge::TextureSlot. Raster uses derivatives, RT uses ray-cone LODs.
public enum SurfaceTexture : uint
{
	Albedo, Normal, Roughness, Metallic, Occlusion, Emissive
};

public interface ISurfaceMaps
{
	float4 sample_texture(SurfaceTexture slot);
}

public float4 linear_vertex_color(MaterialData material, float4 color)
{
	if ((material.flags & SRGB_VERTEX_COLOR) != 0)
		color.rgb = srgb_to_linear(color.rgb);
	return color;
}

public float4 evaluate_base(MaterialData material, float4 albedo, float4 vertex_color)
{
	float4 base = material.albedo * albedo;
	if ((material.flags & VERTEX_COLOR) != 0)
		base *= vertex_color;
	return base;
}

public SurfaceData evaluate_surface<M : ISurfaceMaps>(MaterialData material, M maps, float4 base,
		float3 normal, float3 tangent, float3 binormal, bool is_front_face)
{
	if ((material.flags & HAS_NORMAL_MAP) != 0 && dot(tangent, tangent) > 1e-12)
	{
		tangent = normalize_or(tangent - normal * dot(normal, tangent), float3(0));
		float3 b = cross(normal, tangent);
		b *= dot(b, binormal) < 0.0 ? -1.0 : 1.0;
		float2 xy = maps.sample_texture(SurfaceTexture::Normal).rg * 2.0 - 1.0;
		float z = sqrt(saturate(1.0 - dot(xy, xy)));
		xy *= material.normal_scale;
		normal = normalize_or(tangent * xy.x + b * xy.y + normal * z, normal);
	}

	SurfaceData surface;
	surface.normal_ws = !is_front_face && (material.flags & (DOUBLE_SIDED | CULL_FRONT)) != 0 ? -normal : normal;
	surface.base_color = saturate(base.rgb);
	surface.alpha = saturate(base.a);
	surface.roughness = saturate(material.roughness * dot(maps.sample_texture(SurfaceTexture::Roughness), material.roughness_channel));
	surface.metallic = saturate(material.metallic * dot(maps.sample_texture(SurfaceTexture::Metallic), material.metallic_channel));
	surface.specular = saturate(material.specular);
	surface.material_ao = lerp(1.0, saturate(dot(maps.sample_texture(SurfaceTexture::Occlusion), material.ao_channel)), material.ao_strength);
	surface.emissive = material.emission;
	if ((material.flags & HAS_EMISSION_TEXTURE) != 0)
	{
		float3 texel = maps.sample_texture(SurfaceTexture::Emissive).rgb;
		surface.emissive = (material.flags & EMISSION_OP_MULTIPLY) != 0 ? surface.emissive * texel : surface.emissive + texel;
	}
	surface.emissive *= material.emission_energy;
	surface.shading_model_id = (material.flags & UNLIT) != 0 ? SHADING_UNLIT : SHADING_STANDARD;
	return surface;
}
