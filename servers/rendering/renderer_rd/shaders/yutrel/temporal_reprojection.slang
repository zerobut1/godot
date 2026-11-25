import common;

public struct SurfaceReprojection
{
	public float2 prev_uv;
	public bool moving;
	public bool visible;
};

public SurfaceReprojection reproject_surface(int2 pixel, float2 velocity, float3 world, FrameData frame, FrameData prev)
{
	float2 uv = (pixel + 0.5 - frame.viewport_origin) * frame.inv_viewport_size;
	float4 clip = mul_4x4(prev.unjittered_view_projection, float4(world, 1));
	float2 camera_velocity = motion_vector(mul_4x4(frame.unjittered_view_projection, float4(world, 1)), clip);
	// Account for RG16F velocity quantization so static surfaces keep exact FP32 camera reprojection.
	float2 tol = max(max(float2(1e-4), 0.002 * abs(camera_velocity)), 0.25 * frame.inv_viewport_size);
	SurfaceReprojection result;
	result.moving = any(abs(velocity - camera_velocity) > tol);
	result.prev_uv = uv + (result.moving ? velocity : camera_velocity) + (prev.jitter_uv - frame.jitter_uv);
	result.visible = all(result.prev_uv >= 0) && all(result.prev_uv < 1)
			&& (result.moving || (clip.w > 0 && clip.z >= 0 && clip.z <= clip.w));
	return result;
}

public bool moving_surface_compatible(float3 N, float z, float3 prev_normal, float prev_z)
{
	return prev_z > 0 && dot(N, prev_normal) > 0.75 && abs(z - prev_z) < max(0.15, 0.1 * z);
}

public bool plane_compatible(float4 plane, float4 m3, float4 q, float threshold)
{
	return abs(dot(plane, q)) < threshold * abs(dot(m3, q));
}
