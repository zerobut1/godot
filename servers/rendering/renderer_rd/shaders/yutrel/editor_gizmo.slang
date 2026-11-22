import common;

struct GizmoFrame
{
	float4 view_projection[4];
	float4 camera_x;
	float4 camera_y;
	float4 camera_z;
	float4 camera_position;
	float2 inverse_viewport_size;
	float orthogonal_scale;
	uint convert_to_srgb;
};

struct GizmoData
{
	InstanceData instance;
	float4 albedo;
	float4 modulation;
	float2 uv_scale;
	float alpha_scissor;
	float grid_size;
	float point_size;
	uint flags;
	float2 padding;
};

static const uint GIZMO_VERTEX_COLOR = 1 << 0;
static const uint GIZMO_SRGB_VERTEX_COLOR = 1 << 1;
static const uint BILLBOARD = 1 << 2;
static const uint FIXED_SIZE = 1 << 3;
static const uint GRID = 1 << 4;
static const uint AXIS_RING = 1 << 5;
static const uint VIEW_RING = 1 << 6;
static const uint ORIGIN_AXIS = 1 << 7;
static const uint POINT_SPRITE = 1 << 8;

[[vk::binding(0, 0)]] ConstantBuffer<GizmoFrame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<GizmoData> gizmos;
[[vk::binding(0, 1)]] Texture2D<float4> albedo_texture;
[[vk::binding(1, 1)]] SamplerState albedo_sampler;

struct DrawData
{
	uint draw_id;
};
[[vk::push_constant]] ConstantBuffer<DrawData> draw;

struct VertexInput
{
	[[vk::location(0)]] float4 position : POSITION0;
	[[vk::location(1)]] float2 normal : NORMAL0;
	[[vk::location(3)]] float4 color : COLOR0;
	[[vk::location(4)]] float2 uv : TEXCOORD0;
	uint vertex_id : SV_VulkanVertexID;
};

struct VertexOutput
{
	float4 position : SV_Position;
	[[vk::location(0)]] float4 color : COLOR0;
	[[vk::location(1)]] float2 uv : TEXCOORD0;
	[[vk::location(2)]] float3 world_position : TEXCOORD1;
	[[vk::location(3)]] float3 world_normal : TEXCOORD2;
};

float3 camera_aligned(float3 local)
{
	return frame.camera_x.xyz * local.x + frame.camera_y.xyz * local.y + frame.camera_z.xyz * local.z;
}

float4 project(float3 world)
{
	return mul_4x4(frame.view_projection, float4(world, 1.0));
}

float4 project_origin_axis(float3 vertex, InstanceData instance, float3 start)
{
	// The editor stores the second endpoint in the diagonal of the model basis.
	float3 end = float3(instance.model[0].x, instance.model[1].y, instance.model[2].z);
	float4 a = project(start);
	float4 b = project(end);
	float near_a = a.w - a.z;
	float near_b = b.w - b.z;
	if (near_a < 0.0 && near_b < 0.0)
	{
		return float4(0.0, 0.0, -1.0, 1.0);
	}
	if (near_a < 0.0)
	{
		a = lerp(a, b, near_a / (near_a - near_b));
	}
	else if (near_b < 0.0)
	{
		b = lerp(b, a, near_b / (near_b - near_a));
	}

	float2 delta = (b.xy / b.w - a.xy / a.w) / frame.inverse_viewport_size;
	float2 along = delta * rsqrt(max(dot(delta, delta), 0.0001));
	float2 across = float2(-along.y, along.x);
	float4 clip = lerp(a, b, vertex.z);
	clip.xy += 6.0 * (vertex.x * along + vertex.y * across) * frame.inverse_viewport_size * clip.w;
	return clip;
}

[shader("vertex")]
VertexOutput vertex_main(VertexInput input, out float point_size : SV_PointSize)
{
	GizmoData gizmo = gizmos[draw.draw_id];
	InstanceData instance = gizmo.instance;
	float3 local = unpack_position(input.position, instance);
	float3 origin = mul_3x4(instance.model, float4(0.0, 0.0, 0.0, 1.0));
	float3 normal = unpack_normal(input.normal, input.position.w, instance.flags);
	uint flags = gizmo.flags;

	VertexOutput output;
	point_size = 1.0;
	output.uv = input.uv;
	if (any(gizmo.uv_scale))
	{
		output.uv = (output.uv - 0.5) * gizmo.uv_scale;
	}
	float4 vertex_color = (flags & GIZMO_VERTEX_COLOR) != 0 ? input.color : float4(1.0);
	if ((flags & GIZMO_SRGB_VERTEX_COLOR) != 0)
	{
		vertex_color.rgb = srgb_to_linear(vertex_color.rgb);
	}
	output.color = gizmo.albedo * gizmo.modulation * vertex_color;
	output.world_normal = mul_normal_3x3(instance.normal, normal);

	if ((flags & ORIGIN_AXIS) != 0)
	{
		output.position = project_origin_axis(local, instance, origin);
		output.uv = float2(local.y, 0.0);
		output.world_position = origin;
		return output;
	}
	if ((flags & AXIS_RING) != 0)
	{
		// The editor collapses the ring's cross-section into its centerline.
		float3 radial = mul_3x4(instance.model, float4(local, 0.0));
		if (dot(frame.camera_z.xyz, normalize(radial)) >= -0.005)
		{
			local += normal * 0.02;
		}
	}
	if ((flags & VIEW_RING) != 0)
	{
		local += normal * 0.008;
		float model_scale = length(float3(instance.model[0].x, instance.model[1].x, instance.model[2].x));
		local.x = -local.x;
		output.world_position = origin + camera_aligned(local * model_scale);
	}
	else
	{
		float3 offset = (flags & BILLBOARD) != 0 ? camera_aligned(local) : mul_3x4(instance.model, float4(local, 0.0));
		if ((flags & FIXED_SIZE) != 0)
		{
			offset *= frame.orthogonal_scale > 0.0 ? frame.orthogonal_scale : dot(frame.camera_z.xyz, frame.camera_position.xyz - origin);
		}
		output.world_position = origin + offset;
	}
	output.position = project(output.world_position);
	if ((flags & POINT_SPRITE) != 0)
	{
		// Instanced quads keep handle size independent of depth and Vulkan largePoints support.
		static const float2 corners[6] = {
			float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
			float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0)
		};
		output.uv = corners[input.vertex_id];
		output.position.xy += (output.uv * 2.0 - 1.0) * gizmo.point_size * frame.inverse_viewport_size * output.position.w;
	}
	return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0
{
	GizmoData gizmo = gizmos[draw.draw_id];
	float4 color = input.color;
	if ((gizmo.flags & GRID) != 0)
	{
		float3 normal = normalize(input.world_normal);
		float3 to_camera = frame.camera_position.xyz - input.world_position;
		float3 view = frame.orthogonal_scale > 0.0 ? frame.camera_z.xyz : normalize(to_camera);
		float angle_fade = smoothstep(0.05, 0.2, abs(dot(view, normal)));
		float distance_on_grid = length(to_camera - normal * dot(to_camera, normal));
		float distance_fade = smoothstep(0.02, 0.3, 1.0 - distance_on_grid / gizmo.grid_size);
		color.a *= angle_fade * distance_fade;
	}
	else if ((gizmo.flags & ORIGIN_AXIS) != 0)
	{
		color.a *= 1.0 - smoothstep(0.2, 0.5, abs(input.uv.x));
	}
	else
	{
		color *= albedo_texture.Sample(albedo_sampler, input.uv);
	}
	if (color.a < gizmo.alpha_scissor || color.a <= 0.0)
	{
		discard;
	}
	if (frame.convert_to_srgb != 0)
	{
		color.rgb = linear_to_srgb(color.rgb);
	}
	return color;
}
