import common;
import csm;
import gbuffer;
import ibl_lighting;
import ddgi_common;
import pbr;
import surface;

struct TransparentDirectionalLight
{
	float4 direction;
	float3 radiance;
	uint shadow_index;
};

[[vk::binding(3, 0)]] StructuredBuffer<TransparentDirectionalLight> directional_lights;
[[vk::binding(4, 0)]] StructuredBuffer<CSMShadowData> csm_shadows;
[[vk::binding(5, 0)]] Texture2DArray<float> csm_shadow_array;
[[vk::binding(6, 0)]] SamplerComparisonState comparison_sampler;
[[vk::binding(7, 0)]] Texture2D<float2> dfg_lut;
[[vk::binding(8, 0)]] SamplerState linear_clamp;
[[vk::binding(9, 0)]] TextureCube<float4> prefilter_cubemap;
[[vk::binding(10, 0)]] SamplerState cube_sampler;
[[vk::binding(11, 0)]] StructuredBuffer<float4> sh;
[[vk::binding(12, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(13, 0)]] Texture2DArray<float4> probe_irradiance;
[[vk::binding(14, 0)]] Texture2DArray<float2> probe_distance;

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

[shader("vertex")]
VertexOutput vertex_main(VertexInput input)
{
	return transform_vertex(input, draw.draw_id);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input, bool is_front_face : SV_IsFrontFace) : SV_Target0
{
	float alpha;
	float3 color;
	GBufferData surface = sample_surface(input, is_front_face, true, alpha, color);
	if (surface.shading_model_id == SHADING_STANDARD)
	{
		float3 world = reconstruct_world_position(input.position.xy, input.position.z, frame);
		ShadingData p = prepare_shading(surface, frame, world, dfg_lut, linear_clamp);
		float distance = dot(frame.camera_position.xyz - world, frame.camera_forward.xyz);
		for (uint i = 0; i < draw.light_count; i++)
		{
			TransparentDirectionalLight light = directional_lights[i];
			float3 L = light.direction.xyz;
			float NoL = saturate(dot(p.N, L));
			if (NoL > 0.0)
			{
				float visibility = 1.0;
				if (light.shadow_index != 0xffffffffu)
				{
					CSMShadowData shadow = csm_shadows[light.shadow_index];
					visibility = evaluate_csm_shadow(shadow, csm_shadow_array, comparison_sampler, select_cascade(shadow, distance),
							world, p.N, floor(input.position.xy));
				}
				color += direct_brdf(p, L, NoL) * light.radiance * (NoL * visibility);
			}
		}
		if (draw.has_ibl != 0)
		{
			IBLParams sky = { draw.sky_intensity, draw.sky_sin_rot, draw.sky_cos_rot, draw.prefilter_max_mip, 0 };
			DDGIResources probes = { probe_irradiance, probe_distance, linear_clamp };
			color += evaluate_ibl(p, surface.material_ao, sky, prefilter_cubemap, cube_sampler, sh,
			world, volume, probes);
		}
	}
	color = clamp(sanitize_finite(color), 0.0, 65504.0);
	// DST_COLOR / ZERO blends this factor directly into the destination.
	return float4(multiply ? lerp(1.0, color, alpha) : color, alpha);
}
