import common;
import csm;
import surface;
import transparent_lighting;

[[vk::binding(0, 2)]] StructuredBuffer<CSMShadowData> csm_shadows;
[[vk::binding(1, 2)]] Texture2DArray<float> csm_shadow_array;
[[vk::binding(2, 2)]] SamplerComparisonState comparison_sampler;

struct CascadeShadows : ITransparentShadows
{
	float distance;
	float2 pixel;

	float visibility(TransparentDirectionalLight light, float3 world, float3 normal)
	{
		if (light.shadow_index == 0xffffffffu)
			return 1;
		CSMShadowData shadow = csm_shadows[light.shadow_index];
		return evaluate_csm_shadow(shadow, csm_shadow_array, comparison_sampler, select_cascade(shadow, distance), world, normal, pixel);
	}
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input)
{
	return transparent_vertex(input);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input, bool is_front_face : SV_IsFrontFace) : SV_Target0
{
	float3 world = reconstruct_world_position(input.position.xy, input.position.z, frame);
	CascadeShadows shadows = { dot(frame.camera_position.xyz - world, frame.camera_forward.xyz), floor(input.position.xy) };
	return shade_transparent(input, is_front_face, world, shadows);
}
