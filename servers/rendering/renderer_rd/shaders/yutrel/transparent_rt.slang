import surface;
import local_lights;
import transparent_csm;
import transparent_lighting;

// Include the shared ray queries with the forward pass's shadow descriptor set.
#define YUTREL_RT_SCENE_SET 2
#include "local_light_ray.slang"

struct RayTracedShadows : ITransparentShadows
{
	LocalLightRay ray;
	CascadeShadows cascades;

	float local_visibility(LocalLightData light, float3 L, float dist)
	{
		return ray.local_visibility(light, L, dist);
	}

	float visibility(TransparentDirectionalLight light, float3 world, float3 normal)
	{
		if (light.shadow_index != 0xffffffffu)
			return cascades.visibility(light, world, normal);
		if (light.shadow_opacity <= 0)
			return 1;
		// A single ray to the sun's center gives a stable shadow on forward-blended surfaces.
		if (dot(ray.normal, light.direction) <= 0 || trace_shadow_ray(ray.origin, light.direction, ray.t_min, 1e20))
			return 1 - light.shadow_opacity;
		return 1;
	}
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input)
{
	return transparent_vertex(input);
}

[shader("fragment")]
TransparentTargets fragment_main(VertexOutput input, bool is_front_face : SV_IsFrontFace)
{
	float3 world = reconstruct_world_position(input.position.xy, input.position.z, frame);
	// Evaluate primitive derivatives before alpha discard or any material-dependent branch.
	float3 dx = normalize_or(ddx(world), 0);
	float3 dy = normalize_or(ddy(world), 0);
	float3 Ng = normalize_or(cross(dx, dy), normalize_or(input.normal, float3(0, 0, 1)));
	float3 to_camera = frame.camera_position.xyz - world;
	if (dot(Ng, frame.camera_forward.w != 0 ? frame.camera_forward.xyz : to_camera) < 0)
		Ng = -Ng;
	RayTracedShadows shadows = { { offset_ray_origin(world, Ng), Ng, max(0.002, length(to_camera) * 0.0005) },
			{ dot(to_camera, frame.camera_forward.xyz), floor(input.position.xy) } };
	return shade_transparent(input, is_front_face, world, shadows);
}
