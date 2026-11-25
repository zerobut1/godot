import brdf;
import common;

public static const uint DDGI_NUM_FIXED_RAYS = 32;

public struct DDGIVolumeData
{
	public float3 origin;
	public uint rays_per_probe;
	public float3 spacing;
	public float max_ray_distance;
	public int3 probe_counts;
	public float hysteresis;
	public float4 ray_rotation;
	public float normal_bias, view_bias, distance_exponent, irradiance_gamma;
	public float irradiance_threshold, brightness_threshold, backface_threshold, intensity;
	public uint infinite_bounces, reset_history;
	public float probe_preview_radius;
	public uint active;
	public float min_frontface_distance, fixed_ray_backface_threshold;
	public uint relocation_enabled, classification_enabled;
};

public bool ddgi_uses_fixed_rays(DDGIVolumeData v)
{
	return (v.relocation_enabled != 0 || v.classification_enabled != 0) && v.rays_per_probe > DDGI_NUM_FIXED_RAYS;
}

public uint ddgi_first_blend_ray(DDGIVolumeData v)
{
	return ddgi_uses_fixed_rays(v) ? DDGI_NUM_FIXED_RAYS : 0u;
}

// X runs fastest, then Z, with one XZ plane per array layer.
public int3 ddgi_probe_grid(uint index, DDGIVolumeData v)
{
	return int3(index % v.probe_counts.x, index / (v.probe_counts.x * v.probe_counts.z),
			(index / v.probe_counts.x) % v.probe_counts.z);
}

public int3 ddgi_probe_data_coord(int3 grid)
{
	return int3(grid.x, grid.z, grid.y);
}

public float3 ddgi_probe_base_position(int3 grid, DDGIVolumeData v)
{
	return v.origin + (float3(grid) - 0.5 * (v.probe_counts - 1)) * v.spacing;
}

public float3 ddgi_probe_world_position(int3 grid, DDGIVolumeData v, Texture2DArray<float4> probe_data)
{
	float3 base = ddgi_probe_base_position(grid, v);
	if (v.relocation_enabled != 0 && v.reset_history == 0 && ddgi_uses_fixed_rays(v))
		return base + probe_data.Load(int4(ddgi_probe_data_coord(grid), 0)).xyz * v.spacing;
	return base;
}

public bool ddgi_probe_is_active(int3 grid, DDGIVolumeData v, Texture2DArray<float4> probe_data)
{
	if (v.classification_enabled == 0 || v.reset_history != 0 || !ddgi_uses_fixed_rays(v))
		return true;
	return probe_data.Load(int4(ddgi_probe_data_coord(grid), 0)).w < 0.5;
}

public float3 ddgi_get_probe_uv(int3 grid, float2 oct, int interior, DDGIVolumeData v)
{
	float tile = interior + 2;
	float2 uv = (float2(grid.xz) + 0.5 + oct * (0.5 * interior / tile)) / v.probe_counts.xz;
	return float3(uv, grid.y);
}

public struct DDGIResources
{
	public Texture2DArray<float4> irradiance;
	public Texture2DArray<float2> distance;
	public Texture2DArray<float4> probe_data;
	public SamplerState linear_clamp;
};

float2 fold_oct(float2 p)
{
	return (1 - abs(p.yx)) * select(p >= 0, 1.0, -1.0);
}

public float2 ddgi_oct_encode(float3 n)
{
	float2 p = n.xy * rcp(abs(n.x) + abs(n.y) + abs(n.z));
	return select(bool2(n.z < 0.0), fold_oct(p), p);
}

public float3 ddgi_oct_decode(float2 p)
{
	float3 n = float3(p, 1 - abs(p.x) - abs(p.y));
	if (n.z < 0)
		n.xy = fold_oct(p);
	return normalize(n);
}

public float3 ddgi_spherical_fibonacci(uint ray, uint count)
{
	float phi = 2 * PI * frac(ray * 0.61803398875);
	float z = 1 - (2 * ray + 1.0) / count;
	return float3(float2(cos(phi), sin(phi)) * sqrt(saturate(1 - z * z)), z);
}

public float3 ddgi_ray_direction(uint ray, DDGIVolumeData v)
{
	uint first = ddgi_first_blend_ray(v);
	if (ray < first)
		return normalize(ddgi_spherical_fibonacci(ray, DDGI_NUM_FIXED_RAYS));
	float3 dir = ddgi_spherical_fibonacci(ray - first, v.rays_per_probe - first);
	float4 q = v.ray_rotation;
	return normalize(dir + 2 * cross(q.xyz, cross(q.xyz, dir) + q.w * dir));
}

public float3 ddgi_surface_bias(float3 n, float3 ray_dir, DDGIVolumeData v)
{
	return n * v.normal_bias - ray_dir * v.view_bias;
}

public float ddgi_volume_blend_weight(float3 world, DDGIVolumeData v)
{
	float3 fade = saturate(1 - (abs(world - v.origin) / v.spacing - 0.5 * (v.probe_counts - 1)));
	return fade.x * fade.y * fade.z;
}

public float ddgi_luminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ddgi_eval_volume_irradiance<let FastGamma5 : bool>(float3 world, float3 bias, float3 n, DDGIVolumeData v, DDGIResources probes)
{
	float3 inv_spacing = rcp(v.spacing);
	int3 max_probe = v.probe_counts - 1;
	float3 half_counts = 0.5 * float3(max_probe);
	float3 biased_world = world + bias;
	float3 grid_coord = (biased_world - v.origin) * inv_spacing + half_counts;
	int3 base = int3(clamp(grid_coord, 0.0, float3(max_probe)));
	float3 alpha = saturate(grid_coord - float3(base));

	bool fixed_ready = v.reset_history == 0 && ddgi_uses_fixed_rays(v);
	bool use_classification = fixed_ready && v.classification_enabled != 0;
	bool use_relocation = fixed_ready && v.relocation_enabled != 0;
	bool need_probe_data = use_classification || use_relocation;

	float2 inv_counts_xz = rcp(float2(v.probe_counts.xz));
	float2 dist_uv_bias = 0.5 * inv_counts_xz;
	float2 dist_uv_scale = (7.0 / 16.0) * inv_counts_xz;
	float2 irr_oct_uv = (0.5 + ddgi_oct_encode(n) * (3.0 / 8.0)) * inv_counts_xz;
	float3 grid_origin_to_world = (v.origin - world) - half_counts * v.spacing;
	float half_gamma = 0.5 * v.irradiance_gamma;

	float3 sum = 0;
	float weights = 0;
	for (uint i = 0; i < 8; ++i)
	{
		int3 offset = int3(i, i >> 1, i >> 2) & 1;
		int3 probe = min(base + offset, max_probe);
		float4 pd = need_probe_data ? probes.probe_data.Load(int4(ddgi_probe_data_coord(probe), 0)) : float4(0);
		if (use_classification && pd.w >= 0.5)
			continue;

		float3 p_coord = float3(probe) + (use_relocation ? pd.xyz : float3(0));
		float3 to_probe = grid_origin_to_world + p_coord * v.spacing;
		float3 biased = to_probe - bias;
		float biased_len_sq = dot(biased, biased);
		float2 dist_oct = ddgi_oct_encode(select(bool3(biased_len_sq > 1e-12), -biased, n));

		float2 probe_uv = float2(probe.xz) * inv_counts_xz;
		float2 moments = 2.0 * probes.distance.SampleLevel(probes.linear_clamp, float3(probe_uv + dist_uv_bias + dist_oct * dist_uv_scale, probe.y), 0);
		float3 e = max(probes.irradiance.SampleLevel(probes.linear_clamp, float3(probe_uv + irr_oct_uv, probe.y), 0).rgb, 0.0);

		float to_probe_len_sq = dot(to_probe, to_probe);
		float ndotl = select(to_probe_len_sq > 1e-12, dot(to_probe, n) * rsqrt(max(to_probe_len_sq, 1e-12)), 1.0);
		float wrap = 0.5 * ndotl + 0.5;
		float weight = wrap * wrap + 0.2;

		float diff = sqrt(biased_len_sq) - moments.x;
		if (diff > 0)
		{
			float variance = abs(moments.x * moments.x - moments.y);
			float visibility = variance * rcp(variance + diff * diff);
			weight *= max(0.05, visibility * visibility * visibility);
		}
		weight = select(weight < 0.2, weight * weight * weight * 25.0, weight);
		float3 tri = max(0.001, select(offset != 0, alpha, 1.0 - alpha));
		weight *= tri.x * tri.y * tri.z;

		float3 dec = FastGamma5 ? (e * e * sqrt(e)) : pow(e, half_gamma);
		sum += weight * dec;
		weights += weight;
	}
	if (weights <= 0)
		return float3(0);
	sum *= rcp(weights);
	return sum * sum * (2 * PI);
}

public float3 ddgi_get_volume_irradiance(float3 world, float3 bias, float3 n, DDGIVolumeData v, DDGIResources probes)
{
	if (v.irradiance_gamma == 5.0)
		return ddgi_eval_volume_irradiance<true>(world, bias, n, v, probes);
	return ddgi_eval_volume_irradiance<false>(world, bias, n, v, probes);
}
