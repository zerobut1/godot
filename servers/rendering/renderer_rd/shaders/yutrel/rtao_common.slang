import common;
import temporal_reprojection;

// Working texel (0, 0) samples the center of the first full-resolution pixel.
public int2 rtao_frame_pixel(int2 texel, uint pixel_scale, int2 full_size, float2 viewport_origin)
{
	return min(texel * int(pixel_scale), full_size - 1) + int2(viewport_origin);
}

public float4 load_surface(Texture2D<uint2> surface, int2 texel)
{
	float3 N;
	float depth;
	unpack_surface(surface.Load(int3(texel, 0)), N, depth);
	return float4(N, depth);
}

public float4 surface_clip(int2 pixel, float depth, FrameData frame)
{
	return float4(pixel_center_to_ndc(pixel + 0.5, frame), depth, 1);
}

// History and packed surfaces are viewport-local. Full-resolution velocity is atlas-relative.
public float4 gather_surface_history(Texture2D<uint2> prev_surface, Texture2D<float4> prev_history,
		SurfaceReprojection reproj, float3 world, float3 N, float threshold, FrameData frame, FrameData prev,
		uint pixel_scale, int2 full_size, int2 size, out float weight_sum)
{
	float4 sum = 0;
	weight_sum = 0;
	if (!reproj.visible)
		return sum;
	float4 plane = surface_plane_coeffs(world, N, prev);
	float4 depth_c = reproj.moving ? depth_plane_coeffs(prev) : float4(0);
	float z = dot(frame.camera_position.xyz - world, frame.camera_forward.xyz);
	float4 m3 = prev.inv_view_projection[3];
	float2 prev_work = (reproj.prev_uv / prev.inv_viewport_size - 0.5) / float(pixel_scale);
	int2 base = int2(floor(prev_work));
	float2 f = frac(prev_work);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 tap = base + int2(x, y);
			if (any(tap < 0) || any(tap >= size))
				continue;
			float4 nd = load_surface(prev_surface, tap);
			if (nd.w <= 0 || dot(N, nd.xyz) <= 0.75)
				continue;
			int2 pixel = rtao_frame_pixel(tap, pixel_scale, full_size, prev.viewport_origin);
			float4 q = surface_clip(pixel, nd.w, prev);
			bool valid = reproj.moving ? moving_surface_compatible(N, z, nd.xyz, dot(depth_c, q) / dot(m3, q))
					: plane_compatible(plane, m3, q, threshold);
			if (!valid)
				continue;
			float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			sum += w * prev_history.Load(int3(tap, 0));
			weight_sum += w;
		}
	return sum;
}

public float rtao_inv_sigma(float3 world, float3 camera_position)
{
	return rcp(max(0.015, 0.015 * length(camera_position - world)));
}

public float rtao_surface_weight(float3 world, float3 N, float3 tap_world, float3 tap_N, float inv_sigma)
{
	return exp(-abs(dot(tap_world - world, N)) * inv_sigma) * pow16(saturate(dot(tap_N, N)));
}

groupshared float4 tile_half_world[5][5];
groupshared float4 tile_half_nv[5][5];

public void cache_half_visibility(uint lane, int2 tile_origin, int2 full_size, FrameData frame,
		Texture2D<uint2> half_surface, Texture2D<float> visibility)
{
	if (lane < 25)
	{
		int2 q = int2(lane % 5, lane / 5);
		int2 tap = min(tile_origin + q, (full_size - 1) / 2);
		float4 nd = load_surface(half_surface, tap);
		int2 pixel = rtao_frame_pixel(tap, 2, full_size, frame.viewport_origin);
		float3 world = nd.w > 0 ? reconstruct_world_position(pixel + 0.5, nd.w, frame) : float3(0);
		tile_half_world[q.y][q.x] = float4(world, nd.w);
		tile_half_nv[q.y][q.x] = float4(nd.xyz, visibility.Load(int3(tap, 0)));
	}
	GroupMemoryBarrierWithGroupSync();
}

public float upsample_visibility(int2 local, int2 tile_origin, float3 world, float3 N, float depth, float inv_sigma)
{
	int2 base = local / 2 - tile_origin;
	float2 f = frac(float2(local) * 0.5);
	float sum = 0, weight_sum = 0;
	float nearest = 1, nearest_delta = 2;
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 t = base + int2(x, y);
			float4 wd = tile_half_world[t.y][t.x];
			if (wd.w <= 0)
				continue;
			float4 nv = tile_half_nv[t.y][t.x];
			float delta = abs(wd.w - depth);
			if (delta < nearest_delta)
			{
				nearest_delta = delta;
				nearest = nv.w;
			}
			float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			w *= rtao_surface_weight(world, N, wd.xyz, nv.xyz, inv_sigma);
			sum += w * nv.w;
			weight_sum += w;
		}
	return saturate(weight_sum > 1e-4 ? sum / weight_sum : nearest);
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
