import common;

// Working texel (0, 0) samples the center of the first full-resolution pixel.
public int2 rtao_frame_pixel(int2 texel, uint pixel_scale, int2 full_size, float2 viewport_origin)
{
	return min(texel * int(pixel_scale), full_size - 1) + int2(viewport_origin);
}

public float rtao_inv_sigma(float3 world, float3 camera_position)
{
	return rcp(max(0.015, 0.015 * length(camera_position - world)));
}

public float rtao_surface_weight(float3 world, float3 N, float3 tap_world, float3 tap_N, float inv_sigma)
{
	return exp(-abs(dot(tap_world - world, N)) * inv_sigma) * pow(saturate(dot(tap_N, N)), 64);
}

// Choose a one-sided derivative along each axis, avoiding depth discontinuities.
float3 position_delta(Texture2D<float> scene_depth, FrameData frame, int2 pixel, int2 offset, int2 size, float3 world)
{
	int2 local = pixel + offset - int2(frame.viewport_origin);
	if (any(local < 0) || any(local >= size))
		return float3(1e15);
	float depth = scene_depth.Load(int3(pixel + offset, 0));
	return depth > 0 ? reconstruct_world_position(pixel + offset + 0.5, depth, frame) - world : float3(1e15);
}

float3 derivative(Texture2D<float> scene_depth, FrameData frame, int2 pixel, int2 axis, int2 size, float3 world)
{
	float3 a = -position_delta(scene_depth, frame, pixel, -axis, size, world);
	float3 b = position_delta(scene_depth, frame, pixel, axis, size, world);
	return dot(a, a) < dot(b, b) ? a : b;
}

public uint hash_pixel(uint v)
{
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	return v ^ (v >> 16);
}

public float3 geometric_normal(Texture2D<float> scene_depth, FrameData frame, int2 pixel, int2 full_size, float depth, float3 world, float3 N)
{
	float3 dx = derivative(scene_depth, frame, pixel, int2(1, 0), full_size, world);
	float3 dy = derivative(scene_depth, frame, pixel, int2(0, 1), full_size, world);
	float3 footprint = reconstruct_world_position(pixel + 1.5, depth, frame) - world;
	float limit_sq = max(0.0004, 256 * dot(footprint, footprint));
	float3 Ng = N;
	if (max(dot(dx, dx), dot(dy, dy)) < limit_sq)
	{
		// Normalize derivatives separately so sub-millimeter pixel footprints remain well conditioned.
		Ng = normalize_or(cross(normalize_or(dx, 0), normalize_or(dy, 0)), N);
		if (dot(Ng, N) < 0)
			Ng = -Ng;
	}
	return Ng;
}
