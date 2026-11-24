import common;
import ibl_lighting;
import ddgi_common;
import pbr;
import surface;
import local_lights;

public struct TransparentDirectionalLight
{
	public float3 direction;
	public float shadow_opacity;
	public float3 radiance;
	public uint shadow_index;
};

public interface ITransparentShadows : ILocalLightShadows
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

public struct TransparentTargets
{
	public float4 color : SV_Target0;
	public float reactive : SV_Target1;
};

public TransparentTargets shade_transparent<S : ITransparentShadows>(VertexOutput input, bool is_front_face, float3 world, S shadows)
{
	SurfaceData surface = sample_surface(input, is_front_face, true);
	float3 color = surface.self_illumination();
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
		color += shade_cluster(input.position.xy, world, p, shadows);
		if (draw.has_ibl != 0)
		{
			IBLParams sky = { draw.sky_intensity, draw.sky_sin_rot, draw.sky_cos_rot, draw.prefilter_max_mip, 0, 0, 1 };
			DDGIResources probes = { probe_irradiance, probe_distance, probe_data, linear_clamp };
			color += evaluate_ibl(p, surface.material_ao, sky, prefilter_cubemap, cube_sampler, sh,
					world, volume, probes, float4(0));
		}
	}
	color = clamp(sanitize_finite(color), 0.0, 65504.0);
	// DST_COLOR / ZERO blends this factor directly into the destination.
	TransparentTargets result;
	result.color = float4(multiply ? lerp(1.0, color, surface.alpha) : color, surface.alpha);
	result.reactive = material_blend_mode(input.material_id) == 0 ? saturate(surface.alpha * 0.9)
			: clamp(max(surface.alpha, max(color.r, max(color.g, color.b))) * 0.9, 0.25, 0.9);
	return result;
}
