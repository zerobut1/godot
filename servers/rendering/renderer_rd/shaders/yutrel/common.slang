// Matrices use float4 rows for an explicit CPU/GPU layout.
public struct FrameData
{
	public float4 view_projection[4];
	public float4 inv_view_projection[4];
	public float4 camera_position;
	public float4 camera_forward;
	public float2 viewport_origin;
	public float2 inv_viewport_size;
	public float4 unjittered_view_projection[4];
	public float4 prev_unjittered_view_projection[4];
	public float2 jitter_uv;
	public float2 prev_jitter_uv;
};

public struct InstanceData
{
	public float4 model[3];
	public float4 normal[3];
	public float3 aabb_position;
	public uint flags;
	public float3 aabb_size;
	public uint padding0;
	public float2 uv_scale;
	public uint material_id;
	public uint padding1;
	public float4 prev_model[3];
};

public struct MaterialData
{
	public float4 albedo;
	public float3 emission;
	public float emission_energy;
	public float4 uv_transform;
	public float4 roughness_channel;
	public float4 metallic_channel;
	public float4 ao_channel;
	public float roughness;
	public float metallic;
	public float specular;
	public float normal_scale;
	public float alpha_scissor_threshold;
	public uint flags;
	public float ao_strength;
	public uint blend_mode;
};

public static const uint INSTANCE_COMPRESSED = 1;
public static const uint INSTANCE_HAS_TANGENT = 2;
public static const uint INSTANCE_HAS_COLOR = 4;

public static const uint HAS_NORMAL_MAP = 1;
public static const uint ALPHA_SCISSOR = 2;
public static const uint DOUBLE_SIDED = 4;
public static const uint VERTEX_COLOR = 8;
public static const uint SRGB_VERTEX_COLOR = 16;
public static const uint EMISSION_OP_MULTIPLY = 32;
public static const uint HAS_EMISSION_TEXTURE = 64;
public static const uint UNLIT = 128;

public static const uint CULL_FRONT = 256;
public static const uint TRANSPARENT = 512;

public static const uint SHADING_UNLIT = 0;
public static const uint SHADING_STANDARD = 1;

public struct SurfaceData
{
	public float3 base_color;
	public float3 normal_ws;
	public float roughness;
	public float metallic;
	public float specular;
	public float material_ao;
	public float3 emissive;
	public float alpha;
	public uint shading_model_id;

	public float3 self_illumination()
	{
		return shading_model_id == SHADING_UNLIT ? emissive + base_color : emissive;
	}
};

public float3 mul_3x4(float4 rows[3], float4 v)
{
	return float3(dot(rows[0], v), dot(rows[1], v), dot(rows[2], v));
}

public float4 mul_4x4(float4 rows[4], float4 v)
{
	return float4(dot(rows[0], v), dot(rows[1], v), dot(rows[2], v), dot(rows[3], v));
}

// Backwards, normalized UV displacement. Both clips exclude sample jitter.
public float2 motion_vector(float4 curr, float4 prev)
{
	if (abs(curr.w) < 1e-6 || abs(prev.w) < 1e-6)
		return float2(10.0);
	float2 vel = (prev.xy / prev.w * 0.5 + 0.5) - (curr.xy / curr.w * 0.5 + 0.5);
	return all(isfinite(vel)) ? clamp(vel, -65504.0, 65504.0) : float2(10.0);
}

public float3 mul_normal_3x3(float4 rows[3], float3 v)
{
	return mul_3x4(rows, float4(v, 0.0));
}

public float3 normalize_or(float3 value, float3 fallback)
{
	float length_squared = dot(value, value);
	return length_squared > 1e-12 ? value * rsqrt(length_squared) : fallback;
}

public float3 unpack_position(float4 position, InstanceData instance)
{
	return (instance.flags & INSTANCE_COMPRESSED) != 0 ? position.xyz * instance.aabb_size + instance.aabb_position : position.xyz;
}

public float2 unpack_uv(float2 uv, InstanceData instance)
{
	return (instance.flags & INSTANCE_COMPRESSED) != 0 && any(instance.uv_scale != 0.0) ? (uv - 0.5) * instance.uv_scale : uv;
}

public float3 unpack_octahedral_normal(float2 encoded)
{
	float2 e = encoded * 2.0 - 1.0;
	float3 normal = float3(e, 1.0 - abs(e.x) - abs(e.y));
	normal.xy -= max(-normal.z, 0.0) * sign(normal.xy);
	return normalize(normal);
}

public float2 pack_octahedral_normal(float3 n)
{
	float3 an = abs(n);
	float sum = an.x + an.y + an.z;
	float2 oct = sum > 1e-12 ? n.xy / sum : float2(0);
	if (n.z < 0)
		oct = (1 - abs(oct.yx)) * select(oct >= 0, 1.0, -1.0);
	return saturate(oct * 0.5 + 0.5);
}

public uint2 pack_surface(float3 n, float depth)
{
	uint2 q = uint2(round(pack_octahedral_normal(n) * 65535.0));
	return uint2(q.x | (q.y << 16), asuint(depth));
}

public void unpack_surface(uint2 packed, out float3 n, out float depth)
{
	depth = asfloat(packed.y);
	n = unpack_octahedral_normal(float2(packed.x & 0xffffu, packed.x >> 16) * (1.0 / 65535.0));
}

public float3 unpack_normal(float2 encoded, float tbn_angle, uint flags)
{
	float3 axis = unpack_octahedral_normal(encoded);
	if ((flags & INSTANCE_COMPRESSED) == 0)
	{
		return axis;
	}
	float angle = abs(tbn_angle * 2.0 - 1.0) * 3.141592653589793;
	float c = cos(angle);
	float s = sin(angle);
	return (1.0 - c) * axis.z * axis + float3(-s * axis.y, s * axis.x, c);
}

public void unpack_tbn(float4 axis_tangent, float tbn_angle, uint flags, out float3 normal, out float3 tangent, out float3 binormal)
{
	normal = unpack_normal(axis_tangent.xy, tbn_angle, flags);
	tangent = binormal = float3(0.0);
	if ((flags & INSTANCE_HAS_TANGENT) == 0)
	{
		return;
	}
	float tangent_sign;
	if ((flags & INSTANCE_COMPRESSED) != 0)
	{
		float3 axis = unpack_octahedral_normal(axis_tangent.xy);
		float angle = abs(tbn_angle * 2.0 - 1.0) * 3.141592653589793;
		float c = cos(angle);
		float s = sin(angle);
		tangent = (1.0 - c) * axis.x * axis + float3(c, -s * axis.z, s * axis.y);
		tangent_sign = tbn_angle > 0.5 ? 1.0 : -1.0;
	}
	else
	{
		// MeshStorage packs the tangent into location 1.zw. Location 2 is a dummy attribute.
		float signed_y = axis_tangent.w * 2.0 - 1.0;
		tangent = unpack_octahedral_normal(float2(axis_tangent.z, abs(signed_y)));
		tangent_sign = signed_y >= 0.0 ? 1.0 : -1.0;
	}
	binormal = cross(normal, tangent) * tangent_sign;
}

public float4 fullscreen_position(uint vertex_id)
{
	float2 corner = float2(vertex_id & 1, vertex_id >> 1);
	return float4(corner * 4.0 - 1.0, 0.0, 1.0);
}

public float4 surface_plane_coeffs(float3 world, float3 N, FrameData frame)
{
	return N.x * frame.inv_view_projection[0] + N.y * frame.inv_view_projection[1]
			+ N.z * frame.inv_view_projection[2] - dot(N, world) * frame.inv_view_projection[3];
}

public float4 depth_plane_coeffs(FrameData frame)
{
	float3 fwd = frame.camera_forward.xyz;
	return dot(frame.camera_position.xyz, fwd) * frame.inv_view_projection[3] - fwd.x * frame.inv_view_projection[0]
			- fwd.y * frame.inv_view_projection[1] - fwd.z * frame.inv_view_projection[2];
}

public float2 pixel_center_to_ndc(float2 pixel_center, FrameData frame)
{
	return (pixel_center - frame.viewport_origin) * frame.inv_viewport_size * 2.0 - 1.0;
}

public float pow16(float x)
{
	float x2 = x * x;
	float x4 = x2 * x2;
	float x8 = x4 * x4;
	return x8 * x8;
}

public float3 reconstruct_world_position(float2 pixel, float depth, FrameData frame)
{
	float2 ndc = pixel_center_to_ndc(pixel, frame);
	float4 world = mul_4x4(frame.inv_view_projection, float4(ndc, depth, 1.0));
	return world.xyz / world.w;
}

// The shared white AO fallback has one texel; history textures are viewport-local.
public float load_viewport_ao(Texture2D<float> texture, int2 local_pixel)
{
	uint width, height;
	texture.GetDimensions(width, height);
	return texture.Load(int3(min(local_pixel, int2(width, height) - 1), 0));
}

public float4 load_viewport_reflection(Texture2D<float4> texture, int2 local_pixel)
{
	uint width, height;
	texture.GetDimensions(width, height);
	return texture.Load(int3(min(local_pixel, int2(width, height) - 1), 0));
}

public float3 linear_to_srgb(float3 color)
{
	return select(color <= 0.0031308, 12.92 * color, 1.055 * pow(max(color, 0.0), 1.0 / 2.4) - 0.055);
}

public float3 srgb_to_linear(float3 color)
{
	return select(color <= 0.04045, color / 12.92, pow(max((color + 0.055) / 1.055, 0.0), 2.4));
}

public float3 sanitize_finite(float3 color)
{
	return select(isfinite(color), color, float3(0.0));
}
