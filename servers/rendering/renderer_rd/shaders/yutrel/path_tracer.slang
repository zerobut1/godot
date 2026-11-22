import common;
import brdf;
import ibl_common;
import rt_scene;

struct DirectionalLight
{
	float4 direction;
	float3 radiance;
	uint has_shadow;
};
[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] StructuredBuffer<DirectionalLight> lights;
[[vk::binding(2, 0)]] TextureCube<float4> sky_cubemap;
[[vk::binding(3, 0)]] SamplerState cube_sampler;
[[vk::binding(4, 0)]] RWTexture2D<float4> accum_image;

struct Params
{
	uint2 region_origin, region_size;
	uint sample_index, light_count, has_tlas, has_sky;
	float sky_intensity, sky_sin_rot, sky_cos_rot;
};
[[vk::push_constant]] ConstantBuffer<Params> params;

float random(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return float(((word >> 22u) ^ word) >> 8u) * (1.0 / 16777216.0);
}

// Heitz (2018): sample the projected disk in the stretched view frame.
float3 sample_vndf(float3 v, float alpha, float2 u)
{
	float3 vh = normalize(float3(alpha * v.xy, v.z));
	float3 t = normalize_or(float3(-vh.y, vh.x, 0), float3(1, 0, 0));
	float angle = 2 * PI * u.y;
	float2 disk = sqrt(u.x) * float2(cos(angle), sin(angle));
	disk.y = lerp(sqrt(saturate(1 - disk.x * disk.x)), disk.y, 0.5 * (1 + vh.z));
	float3 h = t * disk.x + cross(vh, t) * disk.y + vh * sqrt(saturate(1 - dot(disk, disk)));
	return normalize(float3(alpha * h.xy, max(h.z, 0.0)));
}

float3 integrate(float3 origin, float3 dir, inout uint rng)
{
	float3 radiance = 0, throughput = 1;
	for (uint bounce = 0; bounce < 4; ++bounce)
	{
		RTHit hit;
		if (params.has_tlas == 0 || !trace_surface(origin, dir, 0, 1e20, hit))
		{
			if (params.has_sky != 0)
			{
				float3 sky_dir = inverse_sky_rotation(dir, params.sky_sin_rot, params.sky_cos_rot);
				radiance += throughput * sky_cubemap.SampleLevel(cube_sampler, sky_dir, 0).rgb * params.sky_intensity;
			}
			break;
		}
		radiance += throughput * hit.emissive;
		if (hit.is_unlit)
		{
			radiance += throughput * hit.base_color;
			break;
		}
		float3 v = -dir, n = hit.shading_normal;
		if (dot(n, v) <= MIN_N_DOT_V)
			n = hit.geometric_normal;
		float nv = max(dot(n, v), MIN_N_DOT_V);
		float roughness = max(MIN_PERCEPTUAL_ROUGHNESS, hit.roughness);
		float alpha = roughness * roughness;
		float3 f0 = standard_f0(hit.base_color, hit.metallic, hit.specular);
		float f90 = saturate(dot(f0, float3(16.5)));
		float3 fv = F_Schlick(f0, f90, nv);
		float3 diffuse = hit.base_color * (1 - hit.metallic) * (1 - fv);
		origin = offset_ray_origin(hit.world_pos, hit.geometric_normal);
		for (uint i = 0; i < params.light_count; ++i)
		{
			float3 l = lights[i].direction.xyz;
			float nl = dot(n, l);
			if (nl <= 0 || dot(hit.geometric_normal, l) <= 0)
				continue;
			if (!trace_shadow_ray(origin, l, 0, 1e20))
			{
				float3 h = normalize(v + l);
				float3 brdf = evaluate_brdf(diffuse, f0, alpha, nv, nl, saturate(dot(n, h)), saturate(dot(l, h)), 1);
				radiance += throughput * brdf * lights[i].radiance * nl;
			}
		}

		float3 t = normalize(cross(abs(n.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0), n));
		float3 b = cross(n, t);
		float p_spec = hit.metallic >= 1 ? 1 : clamp(max(fv.r, max(fv.g, fv.b)), 0.05, 0.95);
		float choice = random(rng);
		float u1 = random(rng);
		float u2 = random(rng);
		if (choice < p_spec)
		{
			float3 h_local = sample_vndf(float3(dot(t, v), dot(b, v), nv), alpha, float2(u1, u2));
			float3 h = t * h_local.x + b * h_local.y + n * h_local.z;
			dir = reflect(-v, h);
			float nl = dot(n, dir);
			if (nl <= 0)
				break;
			// Visible-normal reflection PDF cancels D. G2/G1 = (1+lambda_v)/(1+lambda_v+lambda_l).
			float av = sqrt(nv * nv + alpha * alpha * (1 - nv * nv)) / nv;
			float al = sqrt(nl * nl + alpha * alpha * (1 - nl * nl)) / nl;
			throughput *= F_Schlick(f0, f90, saturate(dot(v, h))) * ((1 + av) / (av + al)) / p_spec;
		}
		else
		{
			float phi = 2 * PI * u2;
			dir = sqrt(u1) * (t * cos(phi) + b * sin(phi)) + n * sqrt(1 - u1);
			throughput *= diffuse / (1 - p_spec);
		}
		if (dot(dir, hit.geometric_normal) <= 0)
			break;
		if (bounce >= 1)
		{
			float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
			if (random(rng) >= survival)
				break;
			throughput /= survival;
		}
	}
	return clamp(sanitize_finite(radiance), 0, 50);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	if (any(tid.xy >= params.region_size))
		return;
	uint2 pixel = tid.xy + params.region_origin;
	uint rng = pixel.x * 1973u + pixel.y * 9277u + params.sample_index * 26699u + 1u;
	float dx = random(rng);
	float dy = random(rng);
	float2 sample = float2(pixel) + float2(dx, dy);
	float3 origin = reconstruct_world_position(sample, 1.0, frame);
	float3 dir = frame.camera_forward.w != 0 ? -frame.camera_forward.xyz : normalize(origin - frame.camera_position.xyz);
	float3 color = integrate(origin, dir, rng);
	if (params.sample_index != 0)
		color = lerp(accum_image[pixel].rgb, color, rcp(float(params.sample_index + 1)));
	accum_image[pixel] = float4(color, 1);
}
