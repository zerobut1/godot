import common;
import rt_scene;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2, 0)]] Texture2D<float> scene_depth;
[[vk::binding(3, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> raw_ao;

struct TraceParams
{
	float radius;
	uint samples, frame_index, has_tlas;
};
[[vk::push_constant]] ConstantBuffer<TraceParams> params;

// Choose a one-sided derivative along each axis, avoiding depth discontinuities.
float3 position_delta(int2 pixel, int2 offset, int2 size, float3 world)
{
	int2 local = pixel + offset - int2(frame.viewport_origin);
	if (any(local < 0) || any(local >= size))
		return float3(1e15);
	float depth = scene_depth.Load(int3(pixel + offset, 0));
	return depth > 0 ? reconstruct_world_position(pixel + offset + 0.5, depth, frame) - world : float3(1e15);
}

float3 derivative(int2 pixel, int2 axis, int2 size, float3 world)
{
	float3 a = -position_delta(pixel, -axis, size, world);
	float3 b = position_delta(pixel, axis, size, world);
	return dot(a, a) < dot(b, b) ? a : b;
}

uint hash_pixel(uint v)
{
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	return v ^ (v >> 16);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	raw_ao.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = int2(tid.xy) + int2(frame.viewport_origin);
	float depth = scene_depth.Load(int3(pixel, 0));
	if (depth <= 0 || params.has_tlas == 0)
	{
		raw_ao[tid.xy] = 1;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = normalize_or(gbuffer_b.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	float3 dx = derivative(pixel, int2(1, 0), size, world);
	float3 dy = derivative(pixel, int2(0, 1), size, world);
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
	float3 origin = offset_ray_origin(world, Ng);
	float t_min = min(0.099 * params.radius, max(0.002, length(frame.camera_position.xyz - world) * 0.0005));
	float3 tangent = normalize(cross(abs(N.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0), N));
	float3 bitangent = cross(N, tangent);
	uint seed = hash_pixel(tid.x + width * tid.y);
	uint2 rotation = uint2(seed, hash_pixel(seed + 1));
	float visibility = 0;
	for (uint i = 0; i < params.samples; ++i)
	{
		// Additive R2 sequence in fixed point, independently rotated at each pixel.
		uint index = params.frame_index * params.samples + i;
		float2 u = float2((rotation + index * uint2(0xc13fa9a9u, 0x91e10da5u)) >> 8) / 16777216.0;
		float angle = 6.283185307 * u.y;
		float3 dir = sqrt(u.x) * (tangent * cos(angle) + bitangent * sin(angle)) + N * sqrt(1 - u.x);
		float cosine = dot(dir, Ng);
		if (cosine <= 0.02)
			dir = normalize(dir + (max(abs(cosine), 0.021) - cosine) * Ng);
		RTHit hit;
		if (trace_surface(origin, dir, t_min, params.radius, hit, true, true))
		{
			float distance_ratio = saturate(hit.t / params.radius);
			visibility += distance_ratio * distance_ratio;
		}
		else
			visibility += 1;
	}
	raw_ao[tid.xy] = visibility / params.samples;
}
