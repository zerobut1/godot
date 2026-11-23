import common;
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

struct UpsampleParams
{
	float max_roughness;
	uint index, has_history, stationary, scene_changed;
};
[[vk::push_constant]] ConstantBuffer<UpsampleParams> params;

float4 load_mirror_guide(int2 p, int2 size)
{
	return all(p >= 0) && all(p < size) ? prev_mirror_guide.Load(int3(p, 0)) : float4(0);
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
	int2 pixel = local + int2(frame.viewport_origin);
	int3 coord = int3(pixel, 0);
	float depth = full_depth.Load(coord);
	float r = gbuffer_c.Load(coord).r;
	if (depth <= 0 || r >= params.max_roughness || gbuffer_a.Load(coord).a == 0)
	{
		reflection[local] = 0;
		mirror_guide[local] = 0;
		return;
	}
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float z = linear_view_depth(world, frame);
	float3 N = reflection_normal(gbuffer_b.Load(coord));
	int2 quad = local / 2;
	if (r < MIRROR_ROUGHNESS)
	{
		float4 g = reflection_guide(N, z, r);
		float4 traced = raw.Load(int3(quad, 0));
		if (all(reflection_pixel(quad, 2, params.index, size) == local) && traced.a >= 0)
		{
			reflection[local] = float4(traced.rgb, 1);
			mirror_guide[local] = g;
			return;
		}
		bool reuse = params.has_history != 0;
		if (reuse && params.scene_changed != 0 && params.stationary != 0 && traced.a >= 0)
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
			if (params.stationary != 0)
				prior = prev_mirror_guide.Load(int3(local, 0));
			else
			{
				previous = int2(floor(previous_pixel(world, prev_frame) + 0.5));
				prior = load_mirror_guide(previous, size);
				if (traced.a > 0 && work_guide.Load(int3(quad, 0)).w < MIRROR_ROUGHNESS)
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
			if (prior.z > 0 && disocclusion(N, linear_view_depth(world, prev_frame), prior) >= 0.9)
			{
				reflection[local] = prev_reflection.Load(int3(previous, 0));
				mirror_guide[local] = g;
				return;
			}
		}
	}
	// An interpolated mirror estimate can be displayed, but only an actual ray
	// or its validated reprojection may seed the next frame's exact history.
	mirror_guide[local] = 0;
	// The converged stationary glossy signal represents the quad center. Using
	// the current ray phase to place it would shift that signal every frame.
	float2 phase = r >= MIRROR_ROUGHNESS && params.stationary != 0 && params.index >= 4 ? float2(0.5) : float2(reflection_phase(params.index, 2));
	float2 p = reflection_texel(local, 2, phase, size);
	int2 base = int2(floor(p));
	float2 f = frac(p);
	float3 sum = 0;
	float weights = 0;
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 2; ++x)
		{
			int3 tc = int3(clamp(base + int2(x, y), 0, (size - 1) / 2), 0);
			float4 g = work_guide.Load(tc);
			if (g.z <= 0 || abs(r - g.w) > 0.15)
				continue;
			float w = pow(saturate(dot(N, unpack_octahedral_normal(g.xy))), normal_sigma(r));
			w *= exp(-abs(z - g.z) / max(0.02, z * 0.04) - 32 * abs(r - g.w));
			w *= (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
			sum += w * resolved.Load(tc).rgb;
			weights += w;
		}
	reflection[local] = weights > 1e-5 ? float4(sum / weights, 1) : float4(0);
}
