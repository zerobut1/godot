import common;
import rtao_common;

// Work and history textures are region-local. G-buffer and output coordinates are absolute.
public struct ShadowGrid
{
	public int2 full_size;
	public uint pixel_scale;
	public uint padding;

	public int2 work_size() { return (full_size + int(pixel_scale) - 1) / int(pixel_scale); }

	public int2 frame_pixel(int2 texel, FrameData frame)
	{
		return rtao_frame_pixel(texel, pixel_scale, full_size, frame.viewport_origin);
	}
};

public bool shadow_surface_compatible(float3 world, float3 N, float threshold, int2 pixel, float depth, float4 normal, FrameData frame)
{
	if (depth <= 0 || dot(N, normalize_or(normal.rgb * 2 - 1, 0)) <= 0.9)
		return false;
	float3 tap_world = reconstruct_world_position(pixel + 0.5, depth, frame);
	return abs(dot(world - tap_world, N)) < threshold;
}
