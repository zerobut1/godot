import common;
import rtao_common;
import rt_scene;
import rt_shadow_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2, 0)]] Texture2D<float> scene_depth;
[[vk::binding(3, 0)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> raw_shadow;

struct TraceParams
{
	float4 direction;
	ShadowGrid grid;
	uint samples;
	uint frame_index, has_tlas;
};
[[vk::push_constant]] ConstantBuffer<TraceParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	int2 size = params.grid.work_size();
	if (any(tid.xy >= uint2(size)))
		return;
	int2 pixel = params.grid.frame_pixel(int2(tid.xy), frame);
	float depth = scene_depth.Load(int3(pixel, 0));
	if (depth <= 0 || params.has_tlas == 0)
	{
		raw_shadow[tid.xy] = float2(1, 0);
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = normalize_or(gbuffer_b.Load(int3(pixel, 0)).rgb * 2 - 1, float3(0, 0, 1));
	float3 Ng = geometric_normal(scene_depth, frame, pixel, params.grid.full_size, depth, world, N);
	float3 L = params.direction.xyz;
	float radius = params.direction.w;
	if (dot(Ng, L) <= -sin(radius))
	{
		raw_shadow[tid.xy] = 0;
		return;
	}
	float3 origin = offset_ray_origin(world, Ng);
	float t_min = max(0.002, length(frame.camera_position.xyz - world) * 0.0005);
	if (radius <= 0)
	{
		raw_shadow[tid.xy] = float2(trace_shadow_ray(origin, L, t_min, 1e20) ? 0 : 1, 0);
		return;
	}
	// A constant-depth neighbor measures the pixel footprint even at a silhouette.
	float footprint = length(reconstruct_world_position(pixel + float2(1.5, 0.5), depth, frame) - world);
	float tan_radius = tan(radius);
	float penumbra_scale = tan_radius / max(footprint * params.grid.pixel_scale, 1e-6);
	uint seed = hash_pixel(tid.x + uint(size.x) * tid.y);
	uint2 rotation = uint2(seed, hash_pixel(seed + 1));
	uint blocked = 0;
	float penumbra = 0;
	for (uint i = 0; i < params.samples; ++i)
	{
		uint index = params.frame_index * params.samples + i;
		float2 u = float2((rotation + index * uint2(0xc13fa9a9u, 0x91e10da5u)) >> 8) / 16777216.0;
		float3 dir = sample_sun_disk(L, tan_radius, u);
		if (dot(dir, Ng) <= 0)
			++blocked;
		else
		{
			float hit_t;
			if (trace_shadow_ray_distance(origin, dir, t_min, 1e20, hit_t))
			{
				++blocked;
				penumbra += min(24.0, hit_t * penumbra_scale);
			}
		}
	}
	raw_shadow[tid.xy] = float2(1 - float(blocked) / params.samples, penumbra / max(blocked, 1u));
}
