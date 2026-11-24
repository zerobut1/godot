import brdf;
import common;
import ddgi_common;
import ibl_common;
import ibl_lighting;
import rt_reflection_common;
import rt_scene;
import local_lights;
import local_light_ray;
import rtao_common;

struct DirectionalLight
{
	float4 direction;
	float3 radiance;
	uint has_shadow;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2, 0)]] Texture2D<float> scene_depth;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> raw_reflection;
[[vk::binding(4, 0)]] StructuredBuffer<uint> ray_list;
[[vk::binding(5, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(6, 0)]] StructuredBuffer<DirectionalLight> lights;
[[vk::binding(7, 0)]] TextureCube<float4> source_cubemap;
[[vk::binding(8, 0)]] TextureCube<float4> prefilter_cubemap;
[[vk::binding(9, 0)]] SamplerState cube_sampler;
[[vk::binding(10, 0)]] StructuredBuffer<float4> sh;
[[vk::binding(11, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(12, 0)]] Texture2DArray<float4> irradiance;
[[vk::binding(13, 0)]] Texture2DArray<float2> distance_atlas;
[[vk::binding(14, 0)]] Texture2DArray<float4> probe_data;
[[vk::binding(15, 0)]] SamplerState linear_clamp;
[[vk::binding(20, 0)]] [[vk::image_format("r8ui")]] Texture2D<uint> quad_offset;

struct TraceParams
{
	int2 full_size;
	uint pixel_scale, dispatch_rows;
	uint samples, frame_index, light_count;
	float sky_intensity, sin_rot, cos_rot, max_mip;
	uint local_count;
};
[[vk::push_constant]] ConstantBuffer<TraceParams> params;

float3 incoming_radiance(float3 origin, float3 dir, float t_min, float r, float2 ray_cone, out float hit_dist)
{
	RTHit hit;
	if (!trace_surface(origin, dir, t_min, 1e20, hit, true, false, ray_cone, r >= 0.15))
	{
		hit_dist = 1e4;
		float3 sky_dir = inverse_sky_rotation(dir, params.sin_rot, params.cos_rot);
		return source_cubemap.SampleLevel(cube_sampler, sky_dir, 0).rgb * params.sky_intensity;
	}
	hit_dist = min(hit.t, 65504.0);
	if (!hit.is_front_face && !hit.is_double_sided)
		return 0;
	if (hit.surface.shading_model_id == SHADING_UNLIT)
		return hit.surface.self_illumination();
	float3 N = dot(hit.surface.normal_ws, -dir) > MIN_N_DOT_V ? hit.surface.normal_ws : hit.geometric_normal;
	float NoV = max(dot(N, -dir), MIN_N_DOT_V);
	float3 f0 = standard_f0(hit.surface.base_color, hit.surface.metallic, hit.surface.specular);
	float3 fresnel = F_Schlick(f0, saturate(dot(f0, float3(16.5))), NoV);
	float3 diffuse = hit.surface.base_color * (1 - hit.surface.metallic) * (1 - fresnel);
	float3 hit_origin = offset_ray_origin(hit.world_pos, hit.geometric_normal);
	float hit_t_min = max(0.002, hit.t * 0.0005);
	float roughness = max(MIN_PERCEPTUAL_ROUGHNESS, hit.surface.roughness);
	float3 radiance = hit.surface.emissive;
	for (uint i = 0; i < params.light_count; ++i)
	{
		float3 L = lights[i].direction.xyz;
		float NoL = dot(N, L);
		if (NoL <= 0 || dot(hit.geometric_normal, L) <= 0 || trace_shadow_ray(hit_origin, L, hit_t_min, 1e20))
			continue;
		float3 H = normalize_or(L - dir, N);
		radiance += evaluate_brdf(diffuse, f0, roughness * roughness, NoV, NoL, saturate(dot(N, H)), saturate(dot(L, H)), 1)
				* lights[i].radiance * NoL;
	}
	LocalLightRay shadow = { hit_origin, hit.geometric_normal, hit_t_min };
	for (uint i = 0; i < params.local_count; ++i)
	{
		float3 L;
		float NoL;
		float3 incident = shadow.incident_light(local_lights[i], hit.world_pos, N, L, NoL);
		if (any(incident != 0))
		{
			float3 H = normalize_or(L - dir, N);
			radiance += incident * evaluate_brdf(diffuse, f0, roughness * roughness, NoV, NoL, saturate(dot(N, H)), saturate(dot(L, H)), 1);
		}
	}
	IBLParams sky = { params.sky_intensity, params.sin_rot, params.cos_rot, params.max_mip, 0, 0, 1 };
	DDGIResources probes = { irradiance, distance_atlas, probe_data, linear_clamp };
	float visibility;
	float3 probe_diffuse;
	float3 indirect = indirect_environment(hit.world_pos, N, dir, sky, sh, volume, probes, visibility, probe_diffuse);
	if (hit.surface.metallic > 0.01)
	{
		float3 R = inverse_sky_rotation(reflect(dir, N), params.sin_rot, params.cos_rot);
		float mip = params.max_mip * roughness * (2 - roughness);
		radiance += prefilter_cubemap.SampleLevel(cube_sampler, R, mip).rgb * f0 * params.sky_intensity * visibility;
	}
	else
	{
		// Reuse low-frequency incident light for the dielectric Fresnel term.
		// Dropping it would darken white surfaces reflected at grazing angles.
		diffuse += fresnel;
	}
	return radiance + diffuse * indirect;
}

float ray_differential(int2 pixel, int2 axis, float3 world, float3 R, float footprint)
{
	int2 local = pixel - int2(frame.viewport_origin);
	int2 neighbor = pixel + (any(local + axis >= params.full_size) ? -axis : axis);
	if (any(neighbor < int2(frame.viewport_origin)))
		return 0;
	float z = scene_depth.Load(int3(neighbor, 0));
	float3 p = reconstruct_world_position(neighbor + 0.5, z, frame);
	if (z <= 0 || length(p - world) > 16 * footprint)
		return 0;
	float3 N = reflection_normal(gbuffer_b.Load(int3(neighbor, 0)));
	return length(reflect(-reflection_view(p, frame), N) - R);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compute_main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	uint index = (group.x * params.dispatch_rows + group.y) * 64 + lane;
	if (index >= ray_list[0])
		return;
	uint packed = ray_list[index + 1];
	uint2 texel = uint2(packed & 0xffffu, packed >> 16);
	int2 pixel = reflection_sample_pixel(int2(texel), params.pixel_scale, quad_offset.Load(int3(texel, 0)), params.full_size) + int2(frame.viewport_origin);
	int3 coord = int3(pixel, 0);
	float depth = scene_depth.Load(coord);
	float r = gbuffer_c.Load(coord).r;
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = reflection_normal(gbuffer_b.Load(coord));
	float3 Ng = geometric_normal(scene_depth, frame, pixel, params.full_size, depth, world, N);
	float3 V = reflection_view(world, frame);
	float footprint = max(length(reconstruct_world_position(pixel + float2(0.5, 1.5), depth, frame) - world), 1e-7);
	float pixel_spread = frame.camera_forward.w != 0 ? 0 : footprint / max(length(frame.camera_position.xyz - world), 1e-5);
	float3 R = reflect(-V, N);
	float curvature = max(ray_differential(pixel, int2(1, 0), world, R, footprint), ray_differential(pixel, int2(0, 1), world, R, footprint));
	float2 ray_cone = float2(footprint, max(pixel_spread, curvature) + r * r);
	float3 origin = offset_ray_origin(world, Ng);
	float t_min = max(0.002, length(frame.camera_position.xyz - world) * 0.0005);
	float3 T = normalize(cross(abs(N.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0), N));
	float3 B = cross(N, T);
	uint width, height;
	raw_reflection.GetDimensions(width, height);
	uint seed = hash_pixel(texel.y * width + texel.x);
	float2 rotation = float2(seed & 65535u, seed >> 16) / 65536.0;
	uint count = r < MIRROR_ROUGHNESS ? 1 : params.samples;
	float4 sum = 0;
	for (uint i = 0; i < count; ++i)
	{
		float3 H = N;
		if (r >= MIRROR_ROUGHNESS)
		{
			float2 u = frac(rotation + float(params.frame_index * count + i) * float2(0.754877666, 0.569840291));
			u.x *= 0.85;
			float3 h = sample_vndf(float3(dot(V, T), dot(V, B), max(dot(V, N), MIN_N_DOT_V)), r * r, u);
			H = T * h.x + B * h.y + N * h.z;
		}
		float3 dir = reflect(-V, H);
		// Fold below-horizon samples above the geometric surface, retaining unit sample weight.
		dir -= 2 * min(dot(dir, Ng), 0.0) * Ng;
		float distance;
		float3 radiance = incoming_radiance(origin, dir, t_min, r, ray_cone, distance);
		sum += float4(radiance, distance);
	}
	float4 avg = sum / count;
	raw_reflection[texel] = float4(clamp(sanitize_finite(avg.rgb), 0, 65504), avg.a);
}
