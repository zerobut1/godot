import common;
import rtao_common;

public float reflection_luma(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

public float3 reflection_compress(float3 c)
{
	return c / (1 + reflection_luma(c));
}

public float3 reflection_decompress(float3 c)
{
	return clamp(c / max(1 - reflection_luma(c), 1e-4), 0, 65504);
}

public float3 reflection_normal(float4 packed)
{
	return normalize_or(packed.rgb * 2 - 1, float3(0, 0, 1));
}

public float3 reflection_view(float3 world, FrameData frame)
{
	return frame.camera_forward.w != 0 ? frame.camera_forward.xyz : normalize_or(frame.camera_position.xyz - world, frame.camera_forward.xyz);
}

public bool reflection_compatible(float3 world, float3 N, float roughness, float threshold,
		int2 pixel, float depth, float4 normal_roughness, FrameData frame)
{
	if (depth <= 0 || dot(N, reflection_normal(normal_roughness)) <= 0.9 || abs(roughness - normal_roughness.a) >= 0.15)
		return false;
	float3 tap_world = reconstruct_world_position(pixel + 0.5, depth, frame);
	return abs(dot(tap_world - world, N)) < threshold;
}

public float reflection_weight(float3 world, float3 N, float r, float3 tap_world, float4 tap, float inv_sigma)
{
	return rtao_surface_weight(world, N, tap_world, reflection_normal(tap), inv_sigma) * exp(-16 * abs(r - tap.a));
}
