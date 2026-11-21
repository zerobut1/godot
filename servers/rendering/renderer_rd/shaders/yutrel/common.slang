// Matrices are stored as float4 rows to make the CPU and GPU layouts explicit.
public struct FrameData
{
	public float4 view_projection[4];
	public float4 light_direction;
	public float3 light_radiance;
	public uint light_cull_mask;
};

public struct InstanceData
{
	public float4 model[3];
	public float4 normal[3];
	public float3 aabb_position;
	public uint flags;
	public float3 aabb_size;
	public uint layer_mask;
};

public static const uint INSTANCE_COMPRESSED = 1;

public float3 mul_3x4(float4 rows[3], float4 v)
{
	return float3(dot(rows[0], v), dot(rows[1], v), dot(rows[2], v));
}

public float4 mul_4x4(float4 rows[4], float4 v)
{
	return float4(dot(rows[0], v), dot(rows[1], v), dot(rows[2], v), dot(rows[3], v));
}

public float3 mul_normal_3x3(float4 rows[3], float3 v)
{
	return mul_3x4(rows, float4(v, 0.0));
}

public float3 unpack_position(float4 position, InstanceData instance)
{
	return (instance.flags & INSTANCE_COMPRESSED) != 0 ? position.xyz * instance.aabb_size + instance.aabb_position : position.xyz;
}

public float3 unpack_octahedral_normal(float2 encoded)
{
	float2 e = encoded * 2.0 - 1.0;
	float3 normal = float3(e, 1.0 - abs(e.x) - abs(e.y));
	normal.xy -= max(-normal.z, 0.0) * sign(normal.xy);
	return normalize(normal);
}

public float3 unpack_normal(float2 encoded, float tbn_angle, uint flags)
{
	float3 axis = unpack_octahedral_normal(encoded);
	if ((flags & INSTANCE_COMPRESSED) == 0)
	{
		return axis;
	}
	// Position.w packs the TBN angle and binormal sign. Rotate +Z around the
	// octahedral axis to recover the normal without reconstructing the tangent.
	float angle = abs(tbn_angle * 2.0 - 1.0) * 3.141592653589793;
	float c = cos(angle);
	float s = sin(angle);
	return (1.0 - c) * axis.z * axis + float3(-s * axis.y, s * axis.x, c);
}

public float3 linear_to_srgb(float3 color)
{
	return select(color <= 0.0031308, 12.92 * color, 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055);
}

public float3 srgb_to_linear(float3 color)
{
	return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
}
