import local_lights;

#ifdef YUTREL_RT_SCENE_SET
#include "rt_scene.slang"
#else
import rt_scene;
#endif

public struct LocalLightRay : ILocalLightShadows
{
	public float3 origin, normal;
	public float t_min;

	public float local_visibility(LocalLightData light, float3 L, float dist)
	{
		if ((light.flags & HAS_SHADOW) == 0 || light.shadow_opacity <= 0)
			return 1;
		if (dot(normal, L) <= 0 || trace_shadow_ray(origin, L, t_min, max(t_min, dist - 0.01)))
			return 1 - light.shadow_opacity;
		return 1;
	}
	// Radiance times cosine at a secondary hit, independent of the camera grid.
	public float3 incident_light(LocalLightData light, float3 world, float3 N, out float3 L, out float NoL)
	{
		float dist;
		float atten = evaluate_local_light_attenuation(light, world, L, dist);
		NoL = saturate(dot(N, L));
		if (atten <= 0 || NoL <= 0)
			return 0;
		return light.radiance * (NoL * atten * local_visibility(light, L, dist));
	}
};
