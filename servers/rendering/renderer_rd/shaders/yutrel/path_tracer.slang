import common;
import brdf;
import ibl_common;
import rt_scene;
import local_lights;
import local_light_ray;

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
	uint local_count;
};
[[vk::push_constant]] ConstantBuffer<Params> params;

float random(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return float(((word >> 22u) ^ word) >> 8u) * (1.0 / 16777216.0);
}

static const uint MAX_BOUNCES = 4;
static const uint MAX_TRANSMISSIONS = 32;
static const uint BLEND_MIX = 0, BLEND_ADD = 1, BLEND_SUB = 2, BLEND_MUL = 3;

bool scatter(RTHit hit, uint bounce, inout uint rng, inout float3 origin, inout float3 dir,
		inout float3 radiance, inout float3 throughput)
{
	SurfaceData surface = hit.surface;
	radiance += throughput * surface.self_illumination();
	if (surface.shading_model_id == SHADING_UNLIT)
		return false;
	float3 v = -dir, n = surface.normal_ws;
	if (dot(n, v) <= MIN_N_DOT_V)
		n = hit.geometric_normal;
	float nv = max(dot(n, v), MIN_N_DOT_V);
	float roughness = max(MIN_PERCEPTUAL_ROUGHNESS, surface.roughness);
	float alpha = roughness * roughness;
	float3 f0 = standard_f0(surface.base_color, surface.metallic, surface.specular);
	float f90 = saturate(dot(f0, float3(16.5)));
	float3 fv = F_Schlick(f0, f90, nv);
	float3 diffuse = surface.base_color * (1 - surface.metallic) * (1 - fv);
	origin = offset_ray_origin(hit.world_pos, hit.geometric_normal);
	for (uint i = 0; i < params.light_count; ++i)
	{
		float3 l = lights[i].direction.xyz;
		float nl = dot(n, l);
		if (nl <= 0)
			continue;
		float3 shadow_dir = l;
		if (lights[i].direction.w > 0)
		{
			float u = random(rng);
			float v_disk = random(rng);
			shadow_dir = sample_sun_disk(l, tan(lights[i].direction.w), float2(u, v_disk));
		}
		if (dot(hit.geometric_normal, shadow_dir) <= 0)
			continue;
		if (!trace_shadow_ray(origin, shadow_dir, 0, 1e20))
		{
			float3 h = normalize(v + l);
			float3 brdf = evaluate_brdf(diffuse, f0, alpha, nv, nl, saturate(dot(n, h)), saturate(dot(l, h)), 1);
			radiance += throughput * brdf * lights[i].radiance * nl;
		}
	}

	LocalLightRay shadow = { origin, hit.geometric_normal, 0 };
	for (uint i = 0; i < params.local_count; ++i)
	{
		float3 l;
		float nl;
		float3 incident = shadow.incident_light(local_lights[i], hit.world_pos, n, l, nl);
		if (any(incident != 0))
		{
			float3 h = normalize_or(v + l, n);
			radiance += throughput * incident * evaluate_brdf(diffuse, f0, alpha, nv, nl, saturate(dot(n, h)), saturate(dot(l, h)), 1);
		}
	}

	float3 t = normalize(cross(abs(n.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0), n));
	float3 b = cross(n, t);
	float p_spec = surface.metallic >= 1 ? 1 : clamp(max(fv.r, max(fv.g, fv.b)), 0.05, 0.95);
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
			return false;
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
		return false;
	if (bounce >= 1)
	{
		float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
		if (random(rng) >= survival)
			return false;
		throughput /= survival;
	}
	return true;
}

// Front-to-back composition of the forward pass's SRC_ALPHA and DST_COLOR equations.
void blend_layer(uint mode, float alpha, float3 color, inout float3 radiance, inout float3 throughput)
{
	color = clamp(sanitize_finite(color), 0, 65504);
	if (mode == BLEND_MUL)
		throughput *= lerp(1.0, color, alpha);
	else
		radiance += throughput * color * (mode == BLEND_SUB ? -alpha : alpha);
}

struct Transmission
{
	float3 origin, dir, radiance, throughput;
	uint bounce, mode;
	float alpha;
};

float3 integrate(float3 origin, float3 dir, inout uint rng)
{
	float3 radiance = 0, throughput = 1;
	uint bounce = 0, transmissions = 0, pending_count = 0;
	// Only a lit ADD/SUB/MUL layer needs a second path to evaluate its source color.
	// Its reflected branch consumes a bounce before another transmission can be saved.
	Transmission pending[MAX_BOUNCES] = {};
	for (;;)
	{
		RTHit hit;
		if (bounce < MAX_BOUNCES && params.has_tlas != 0 &&
				trace_surface(origin, dir, 0, 1e20, hit, false, false, float2(0), false, transmissions < MAX_TRANSMISSIONS))
		{
			if (hit.transparent)
			{
				float alpha = hit.surface.alpha;
				float3 behind = offset_ray_origin(hit.world_pos, -hit.geometric_normal);
				if (alpha <= 0 || (hit.blend_mode == BLEND_MIX && alpha < 1 && random(rng) >= alpha))
				{
					transmissions++;
					origin = behind;
					continue;
				}
				if (hit.blend_mode != BLEND_MIX)
				{
					transmissions++;
					if (hit.surface.shading_model_id == SHADING_UNLIT)
					{
						blend_layer(hit.blend_mode, alpha, hit.surface.self_illumination(), radiance, throughput);
						origin = behind;
						continue;
					}
					Transmission layer = { behind, dir, radiance, throughput, bounce, hit.blend_mode, alpha };
					pending[pending_count++] = layer;
					radiance = 0;
					throughput = 1;
				}
			}
			if (scatter(hit, bounce, rng, origin, dir, radiance, throughput))
			{
				bounce++;
				continue;
			}
		}
		else if (bounce < MAX_BOUNCES && params.has_sky != 0)
		{
			float3 sky_dir = inverse_sky_rotation(dir, params.sky_sin_rot, params.sky_cos_rot);
			radiance += throughput * sky_cubemap.SampleLevel(cube_sampler, sky_dir, 0).rgb * params.sky_intensity;
		}

		if (pending_count == 0)
			break;
		float3 layer_color = radiance;
		Transmission layer = pending[--pending_count];
		origin = layer.origin;
		dir = layer.dir;
		bounce = layer.bounce;
		radiance = layer.radiance;
		throughput = layer.throughput;
		blend_layer(layer.mode, layer.alpha, layer_color, radiance, throughput);
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
