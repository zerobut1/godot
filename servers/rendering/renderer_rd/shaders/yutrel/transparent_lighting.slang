import common;
import gbuffer;
import ibl_lighting;
import ddgi_common;
import pbr;
import surface;

public struct TransparentDirectionalLight
{
	public float3 direction;
	public float shadow_opacity;
	public float3 radiance;
	public uint shadow_index;
};

public interface ITransparentShadows
{
	float visibility(TransparentDirectionalLight light, float3 world, float3 normal);
}

[[vk::binding(3, 0)]] StructuredBuffer<TransparentDirectionalLight> directional_lights;
[[vk::binding(7, 0)]] Texture2D<float2> dfg_lut;
[[vk::binding(8, 0)]] SamplerState linear_clamp;
[[vk::binding(9, 0)]] TextureCube<float4> prefilter_cubemap;
[[vk::binding(10, 0)]] SamplerState cube_sampler;
[[vk::binding(11, 0)]] StructuredBuffer<float4> sh;
[[vk::binding(12, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(13, 0)]] Texture2DArray<float4> probe_irradiance;
[[vk::binding(14, 0)]] Texture2DArray<float2> probe_distance;
[[vk::binding(15, 0)]] Texture2DArray<float4> probe_data;

struct DrawData
{
	uint draw_id;
	uint light_count;
	uint has_ibl;
	float sky_intensity;
	float sky_sin_rot;
	float sky_cos_rot;
	float prefilter_max_mip;
	uint pad;
};
[[vk::push_constant]] ConstantBuffer<DrawData> draw;
[[vk::constant_id(0)]] const bool multiply = false;

public VertexOutput transparent_vertex(VertexInput input)
{
	return transform_vertex(input, draw.draw_id);
}

public float4 shade_transparent<S : ITransparentShadows>(VertexOutput input, bool is_front_face, float3 world, S shadows)
{
	float alpha;
	float3 color;
	GBufferData surface = sample_surface(input, is_front_face, true, alpha, color);
	if (surface.shading_model_id == SHADING_STANDARD)
	{
		ShadingData p = prepare_shading(surface, frame, world, dfg_lut, linear_clamp);
		for (uint i = 0; i < draw.light_count; i++)
		{
			TransparentDirectionalLight light = directional_lights[i];
			float3 L = light.direction;
			float NoL = saturate(dot(p.N, L));
			if (NoL > 0.0)
			{
				float visibility = shadows.visibility(light, world, p.N);
				color += direct_brdf(p, L, NoL) * light.radiance * (NoL * visibility);
			}
		}
		if (draw.has_ibl != 0)
		{
			IBLParams sky = { draw.sky_intensity, draw.sky_sin_rot, draw.sky_cos_rot, draw.prefilter_max_mip, 0 };
			DDGIResources probes = { probe_irradiance, probe_distance, probe_data, linear_clamp };
			color += evaluate_ibl(p, surface.material_ao, sky, prefilter_cubemap, cube_sampler, sh,
					world, volume, probes);
		}
	}
	color = clamp(sanitize_finite(color), 0.0, 65504.0);
	// DST_COLOR / ZERO blends this factor directly into the destination.
	return float4(multiply ? lerp(1.0, color, alpha) : color, alpha);
}
