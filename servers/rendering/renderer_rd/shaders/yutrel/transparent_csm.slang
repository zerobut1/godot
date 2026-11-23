import csm;
import transparent_lighting;
import local_lights;

[[vk::binding(0, 3)]] StructuredBuffer<CSMShadowData> csm_shadows;
[[vk::binding(1, 3)]] Texture2DArray<float> csm_shadow_array;
[[vk::binding(2, 3)]] SamplerComparisonState comparison_sampler;

public struct CascadeShadows : ITransparentShadows
{
	public float distance;
	public float2 pixel;

	public float local_visibility(LocalLightData light, float3 L, float dist) { return 1; }

	public float visibility(TransparentDirectionalLight light, float3 world, float3 normal)
	{
		if (light.shadow_index == 0xffffffffu)
			return 1;
		CSMShadowData shadow = csm_shadows[light.shadow_index];
		return evaluate_csm_shadow(shadow, csm_shadow_array, comparison_sampler, select_cascade(shadow, distance), world, normal, pixel);
	}
};
