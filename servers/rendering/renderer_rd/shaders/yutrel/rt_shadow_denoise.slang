import common;
import rtao_common;
import rt_shadow_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<uint2> work_surface;
[[vk::binding(3, 0)]] Texture2D<float2> input_shadow;
[[vk::binding(4, 0)]] [[vk::image_format("rg16f")]] RWTexture2D<float2> intermediate;
[[vk::binding(5, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> resolved;

struct FilterParams
{
	ShadowGrid grid;
	int step_size;
	uint is_final;
	float opacity;
};
[[vk::push_constant]] ConstantBuffer<FilterParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	int2 size = params.grid.work_size();
	if (any(tid.xy >= uint2(size)))
		return;
	int2 center = int2(tid.xy);
	int2 pixel = params.grid.frame_pixel(center, frame);
	float4 nd = load_surface(work_surface, center);
	float depth = nd.w;
	float2 result = input_shadow.Load(int3(center, 0));
	if (depth > 0)
	{
		float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
		float3 N = nd.xyz;
		float4 plane_c_scaled = surface_plane_coeffs(world, N, frame) * rtao_inv_sigma(world, frame.camera_position.xyz);
		float4 m3 = frame.inv_view_projection[3];
		// Axial and diagonal taps have just two distinct squared distances.
		// Gather each tap once, then evaluate the Gaussian using the neighborhood radius.
		float2 axial = 0, diagonal = 0;
		for (int y = -1; y <= 1; ++y)
			for (int x = -1; x <= 1; ++x)
			{
				int2 tap = center + int2(x, y) * params.step_size;
				if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size))
					continue;
				float4 tap_nd = load_surface(work_surface, tap);
				if (tap_nd.w <= 0)
					continue;
				int2 p = params.grid.frame_pixel(tap, frame);
				float4 clip = surface_clip(p, tap_nd.w, frame);
				float plane_distance = abs(dot(plane_c_scaled, clip) / dot(m3, clip));
				float2 sample = input_shadow.Load(int3(tap, 0));
				float cosine = saturate(dot(tap_nd.xyz, N));
				if (cosine > 0.9 && plane_distance < 1.33)
					result.g = max(result.g, sample.g);
				float weight = exp(-plane_distance) * pow16(cosine);
				float2 tap_sum = float2(sample.r, 1) * weight;
				if (x == 0 || y == 0)
					axial += tap_sum;
				else
					diagonal += tap_sum;
			}
		float sigma = max(0.35, result.g * 0.5);
		float spatial = exp(-float(params.step_size * params.step_size) / (2 * sigma * sigma));
		float2 total = float2(result.r, 1) + spatial * axial + spatial * spatial * diagonal;
		result.r = saturate(total.x / total.y);
	}
	if (params.is_final == 0)
		intermediate[center] = result;
	else if (params.grid.pixel_scale == 1)
		resolved[pixel] = lerp(1.0, result.r, params.opacity);
	else
		resolved[center] = result.r;
}
