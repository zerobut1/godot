import common;
import rtao_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] Texture2D<uint2> work_surface;
[[vk::binding(3, 0)]] Texture2D<float2> input_ao;
[[vk::binding(4, 0)]] [[vk::image_format("r16f")]] RWTexture2D<float> output_ao;

struct FilterParams
{
	int2 full_size;
	uint pixel_scale;
	int step_size;
	uint is_final;
	float intensity;
};
[[vk::push_constant]] ConstantBuffer<FilterParams> params;

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID)
{
	uint width, height;
	output_ao.GetDimensions(width, height);
	int2 size = int2(width, height);
	if (any(tid.xy >= uint2(size)))
		return;
	int2 center_coord = int2(tid.xy);
	float4 nd = load_surface(work_surface, center_coord);
	float depth = nd.w;
	if (depth <= 0)
	{
		output_ao[tid.xy] = 1;
		return;
	}
	int2 pixel = rtao_frame_pixel(center_coord, params.pixel_scale, params.full_size, frame.viewport_origin);
	float3 world = reconstruct_world_position(pixel + 0.5, depth, frame);
	float3 N = nd.xyz;
	float4 plane_c_scaled = surface_plane_coeffs(world, N, frame) * rtao_inv_sigma(world, frame.camera_position.xyz);
	float4 m3 = frame.inv_view_projection[3];
	float2 center = input_ao.Load(int3(center_coord, 0));
	float sum = 0.25 * center.r, weight_sum = 0.25;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			int2 tap = center_coord + int2(x, y) * params.step_size;
			if ((x == 0 && y == 0) || any(tap < 0) || any(tap >= size))
				continue;
			float4 tap_nd = load_surface(work_surface, tap);
			if (tap_nd.w <= 0)
				continue;
			int2 p = rtao_frame_pixel(tap, params.pixel_scale, params.full_size, frame.viewport_origin);
			float4 clip = surface_clip(p, tap_nd.w, frame);
			float plane_distance = abs(dot(plane_c_scaled, clip) / dot(m3, clip));
			float w = (x == 0 || y == 0) ? 0.125 : 0.0625;
			w *= exp(-plane_distance) * pow16(saturate(dot(tap_nd.xyz, N)));
			float2 value = input_ao.Load(int3(tap, 0));
			if (params.step_size == 1)
				w *= min(1, value.g / center.g);
			sum += w * value.r;
			weight_sum += w;
		}
	float filtered = saturate(sum / weight_sum);
	if (params.is_final != 0)
		filtered = params.intensity > 0 ? pow(filtered, params.intensity) : 1;
	output_ao[tid.xy] = filtered;
}
