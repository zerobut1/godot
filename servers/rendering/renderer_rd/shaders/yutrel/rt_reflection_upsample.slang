import common;
import temporal_reprojection;
import rt_reflection_common;

[[vk::binding(0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1)]] Texture2D<float4> gbuffer_b;
[[vk::binding(2)]] Texture2D<float> full_depth;
[[vk::binding(3)]] Texture2D<float4> resolved;
[[vk::binding(4)]] Texture2D<float4> work_guide;
[[vk::binding(5)]] Texture2D<float4> raw;
[[vk::binding(6)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> reflection;
[[vk::binding(7)]] Texture2D<float4> gbuffer_c;
[[vk::binding(8)]] Texture2D<float4> gbuffer_a;
[[vk::binding(9)]] ConstantBuffer<FrameData> prev_frame;
[[vk::binding(10)]] Texture2D<float4> prev_reflection;
[[vk::binding(11)]] Texture2D<float4> prev_mirror_guide;
[[vk::binding(12)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> mirror_guide;

[[vk::binding(13)]] Texture2D<float2> velocity;
[[vk::binding(14)]] StructuredBuffer<uint> mirror_flags;

struct UpsampleParams
{
	float max_roughness;
	uint index, has_history, stationary, scene_changed, curr_slot;
};
[[vk::push_constant]] ConstantBuffer<UpsampleParams> params;

float4 load_mirror_guide(int2 p, int2 size)
{
	return all(p >= 0) && all(p < size) ? prev_mirror_guide.Load(int3(p, 0)) : float4(0);
}

void consider_fallback(int2 tc, float4 g, float z, float r, inout float best_score, inout int2 best_tap)
{
	float dz = abs(z - g.z);
	if (g.z > 0 && dz < max(0.15, z * 0.25))
	{
		float score = dz / max(0.02, z * 0.04) + 12.0 * abs(r - g.w);
		if (score < best_score)
		{
			best_score = score;
			best_tap = tc;
		}
	}
}

bool resolve_mirror(int2 local, int2 pixel, int2 size, float3 world, float3 N, float z, float r,
		bool curr_has_mirror, bool prev_has_mirror)
{
	int2 quad = local / 2;
	float4 g = reflection_guide(N, z, r);
	float4 traced = raw.Load(int3(quad, 0));
	if (all(reflection_pixel(quad, 2, params.index, size) == local) && traced.a >= 0)
	{
		reflection[local] = float4(traced.rgb, 1);
		if (curr_has_mirror)
			mirror_guide[local] = g;
		return true;
	}
	float2 vel = velocity.Load(int3(pixel, 0));
	bool stationary = params.stationary != 0 && all(abs(vel) < 1e-5);
	SurfaceReprojection reproj = reproject_surface(pixel, vel, world, frame, prev_frame);
	bool reuse = params.has_history != 0 && prev_has_mirror && (stationary || reproj.visible);
	if (reuse && params.scene_changed != 0 && stationary && traced.a >= 0)
	{
		// An off-screen mover can change a stationary mirror. Invalidate the
		// affected quad, preserving unrelated mirrors in an animated scene.
		int2 sampled = reflection_pixel(quad, 2, params.index, size);
		if (prev_mirror_guide.Load(int3(sampled, 0)).z > 0)
			reuse = temporal_variance(prev_reflection.Load(int3(sampled, 0)).rgb, traced.rgb) < 0.01;
	}
	if (reuse)
	{
		int2 previous = local;
		float4 prior;
		if (stationary)
			prior = prev_mirror_guide.Load(int3(local, 0));
		else
		{
			previous = int2(floor(reproj.prev_uv / prev_frame.inv_viewport_size));
			prior = load_mirror_guide(previous, size);
			if (!reproj.moving && params.stationary == 0 && traced.a > 0 && work_guide.Load(int3(quad, 0)).w < MIRROR_ROUGHNESS)
			{
				int2 hit = int2(floor(previous_pixel(world - reflection_view(world, frame) * traced.a, prev_frame) + 0.5));
				float4 hg = load_mirror_guide(hit, size);
				if (use_virtual_hit(N, r, hg, prior))
				{
					previous = hit;
					prior = hg;
				}
			}
		}
		bool compatible = reproj.moving ? moving_surface_compatible(N, z, unpack_octahedral_normal(prior.xy), prior.z)
				: prior.z > 0 && disocclusion(N, linear_view_depth(world, prev_frame), prior) >= 0.9;
		if (compatible)
		{
			reflection[local] = prev_reflection.Load(int3(previous, 0));
			if (curr_has_mirror)
				mirror_guide[local] = g;
			return true;
		}
	}
	return false;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	reflection.GetDimensions(width, height);
	int2 size = int2(width, height);
	int2 local = int2(tid.xy);
	if (any(local >= size))
		return;
	bool curr_has_mirror = mirror_flags[params.curr_slot] != 0;
	int2 pixel = local + int2(frame.viewport_origin);
	int3 coord = int3(pixel, 0);
	float depth = full_depth.Load(coord);
	float r = gbuffer_c.Load(coord).r;
	if (depth <= 0 || r >= params.max_roughness || gbuffer_a.Load(coord).a == 0)
	{
		reflection[local] = 0;
		if (curr_has_mirror)
			mirror_guide[local] = 0;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float z = linear_view_depth(world, frame);
	float3 N = reflection_normal(gbuffer_b.Load(coord));
	int2 quad = local / 2;
	if (r < MIRROR_ROUGHNESS)
	{
		bool prev_has_mirror = mirror_flags[1 - params.curr_slot] != 0;
		if (resolve_mirror(local, pixel, size, world, N, z, r, curr_has_mirror, prev_has_mirror))
			return;
	}
	// Only a traced or reprojected mirror can seed exact history.
	if (curr_has_mirror)
		mirror_guide[local] = 0;
	float2 phase = r >= MIRROR_ROUGHNESS ? float2(0.0) : float2(reflection_phase(params.index, 2));
	float2 p = reflection_texel(local, 2, phase, size);
	int2 base = int2(floor(p));
	float2 f = frac(p);
	int2 max_half = (size - 1) / 2;
	float3 sum = 0;
	float weights = 0;
	float best_score = 1e20;
	int2 best_tap = int2(-1);
	float inv_z_tol = rcp(max(0.02, z * 0.04));
	float nsig = normal_sigma(r);
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int2 tc = clamp(base + int2(x, y), 0, max_half);
			float4 g = work_guide.Load(int3(tc, 0));
			consider_fallback(tc, g, z, r, best_score, best_tap);
			if (g.z <= 0 || abs(r - g.w) > 0.25 || (r >= MIRROR_ROUGHNESS && g.w < MIRROR_ROUGHNESS))
				continue;
			float w = pow(saturate(dot(N, unpack_octahedral_normal(g.xy))), nsig);
			w *= exp(-abs(z - g.z) * inv_z_tol - 32 * abs(r - g.w));
			w *= (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			sum += w * resolved.Load(int3(tc, 0)).rgb;
			weights += w;
		}
	if (weights > 1e-5)
	{
		reflection[local] = float4(sum / weights, 1);
		return;
	}
	if (any(quad < base) || any(quad > base + 1))
	{
		consider_fallback(quad, work_guide.Load(int3(quad, 0)), z, r, best_score, best_tap);
	}
	reflection[local] = best_tap.x >= 0 ? float4(resolved.Load(int3(best_tap, 0)).rgb, 1) : float4(0);
}
