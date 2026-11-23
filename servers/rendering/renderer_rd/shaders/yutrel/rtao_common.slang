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
