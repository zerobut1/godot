import brdf;
import common;

// Y-up, X-fastest. ProbeData in Step 2B uses int3(grid.x, grid.z, grid.y)
// with xyz = relocation / spacing and w = state. All probes currently have zero offset.
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
};

public float3 ddgi_probe_world_position(int3 grid, DDGIVolumeData v)
{
	return v.origin + (float3(grid) - 0.5 * (v.probe_counts - 1)) * v.spacing;
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
	public SamplerState linear_clamp;
};

float2 fold_oct(float2 p)
{
	return (1 - abs(p.yx)) * select(p >= 0, 1.0, -1.0);
}

public float2 ddgi_oct_encode(float3 n)
{
	float2 p = n.xy / dot(abs(n), float3(1));
	return n.z < 0 ? fold_oct(p) : p;
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
	float3 dir = ddgi_spherical_fibonacci(ray, v.rays_per_probe);
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

public float3 ddgi_get_volume_irradiance(float3 world, float3 bias, float3 n, DDGIVolumeData v, DDGIResources probes)
{
	float3 grid = clamp((world + bias - v.origin) / v.spacing + 0.5 * (v.probe_counts - 1), 0, v.probe_counts - 1);
	int3 base = int3(grid);
	float3 alpha = grid - base;
	float3 sum = 0;
	float weights = 0;
	float2 oct = ddgi_oct_encode(n);
	for (uint i = 0; i < 8; ++i)
	{
		int3 offset = int3(i, i >> 1, i >> 2) & 1;
		int3 probe = min(base + offset, v.probe_counts - 1);
		float3 to_probe = ddgi_probe_world_position(probe, v) - world;
		float wrap = 0.5 * (dot(normalize_or(to_probe, n), n) + 1);
		float weight = wrap * wrap + 0.2;
		float3 biased = to_probe - bias;
		float2 moments = 2 * probes.distance.SampleLevel(probes.linear_clamp, ddgi_get_probe_uv(probe, ddgi_oct_encode(normalize_or(-biased, n)), 14, v), 0);
		float diff = length(biased) - moments.x;
		if (diff > 0)
		{
			float variance = abs(moments.x * moments.x - moments.y);
			float visibility = variance / (variance + diff * diff);
			weight *= max(0.05, visibility * visibility * visibility);
		}
		if (weight < 0.2)
			weight *= weight * weight / (0.2 * 0.2);
		float3 tri = max(0.001, lerp(1 - alpha, alpha, float3(offset)));
		weight *= tri.x * tri.y * tri.z;
		float3 encoded = probes.irradiance.SampleLevel(probes.linear_clamp, ddgi_get_probe_uv(probe, oct, 6, v), 0).rgb;
		sum += weight * pow(max(encoded, 0), 0.5 * v.irradiance_gamma);
		weights += weight;
	}
	sum /= weights;
	return sum * sum * (2 * PI);
}
