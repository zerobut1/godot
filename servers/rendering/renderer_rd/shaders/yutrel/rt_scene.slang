import common;

struct RTVertex
{
	float3 position; float u;
	float3 normal; float v;
	float4 tangent;
	float4 color;
};

struct RTInstance
{
	float4 model[3];
	float4 normal[3];
	uint first_surface, surface_count, flags, padding;
};

struct RTSurface
{
	uint vertex_offset, index_offset, material_id, flags;
};

struct RTMaterialTextures
{
	uint albedo, normal, roughness, metallic, ao, emissive;
	uint2 padding;
};

[[vk::binding(0, 1)]] RaytracingAccelerationStructure rt_tlas;
[[vk::binding(1, 1)]] StructuredBuffer<RTVertex> rt_vertices;
[[vk::binding(2, 1)]] StructuredBuffer<uint> rt_indices;
[[vk::binding(3, 1)]] StructuredBuffer<RTInstance> rt_instances;
[[vk::binding(4, 1)]] StructuredBuffer<RTSurface> rt_surfaces;
[[vk::binding(5, 1)]] StructuredBuffer<MaterialData> rt_materials;
[[vk::binding(6, 1)]] StructuredBuffer<RTMaterialTextures> rt_material_textures;
[[vk::binding(7, 1)]] SamplerState rt_sampler;
[[vk::binding(8, 1)]] Texture2D<float4> rt_textures[512];

public struct RTHit
{
	public float3 world_pos, geometric_normal, shading_normal, base_color, emissive;
	public float roughness, metallic, specular, t;
	public bool is_front_face, is_unlit, is_double_sided;
};

public float3 offset_ray_origin(float3 p, float3 n)
{
	// Wächter and Binder, Ray Tracing Gems, chapter 6.
	int3 steps = int3(256.0 * n);
	float3 offset = asfloat(asint(p) + select(p < 0, -steps, steps));
	return select(abs(p) < 1.0 / 32.0, p + n / 65536.0, offset);
}

float4 sample_map(uint index, float2 uv)
{
	return rt_textures[NonUniformResourceIndex(index)].SampleLevel(rt_sampler, uv, 0.0);
}

struct Triangle
{
	RTVertex a, b, c;
	float3 weights;

	float2 uv(MaterialData mat)
	{
		float2 uv0 = float2(dot(weights, float3(a.u, b.u, c.u)), dot(weights, float3(a.v, b.v, c.v)));
		return uv0 * mat.uv_transform.xy + mat.uv_transform.zw;
	}

	float3 face_normal(RTInstance inst)
	{
		// Godot triangles wind clockwise. Restore object orientation after a reflection.
		float3 ab = mul_normal_3x3(inst.model, b.position - a.position);
		float3 ac = mul_normal_3x3(inst.model, c.position - a.position);
		return normalize(cross(ac, ab)) * ((inst.flags & 1) != 0 ? -1.0 : 1.0);
	}
};

Triangle load_triangle(RTSurface s, uint primitive, float2 bary)
{
	uint first = s.index_offset + 3 * primitive;
	Triangle tri;
	tri.a = rt_vertices[s.vertex_offset + rt_indices[first]];
	tri.b = rt_vertices[s.vertex_offset + rt_indices[first + 1]];
	tri.c = rt_vertices[s.vertex_offset + rt_indices[first + 2]];
	tri.weights = float3(1.0 - bary.x - bary.y, bary);
	return tri;
}

static const uint CULL_FRONT = 256;

bool accept_candidate(uint instance, uint geometry, uint primitive, float2 bary, float3 dir, bool shadow, bool include_backfaces = false)
{
	RTInstance inst = rt_instances[instance];
	RTSurface s = rt_surfaces[inst.first_surface + geometry];
	if ((s.flags & 1) != 0 || (!shadow && (inst.flags & 2) != 0))
		return false;
	MaterialData mat = rt_materials[s.material_id];
	bool check_side = !include_backfaces && (mat.flags & DOUBLE_SIDED) == 0 && (!shadow || (mat.flags & CULL_FRONT) != 0);
	if (!check_side && (mat.flags & ALPHA_SCISSOR) == 0)
		return true;
	Triangle tri = load_triangle(s, primitive, bary);
	if (check_side && ((dot(tri.face_normal(inst), dir) < 0) == ((mat.flags & CULL_FRONT) != 0)))
		return false;
	if ((mat.flags & ALPHA_SCISSOR) == 0)
		return true;
	float alpha = mat.albedo.a * sample_map(rt_material_textures[s.material_id].albedo, tri.uv(mat)).a;
	if ((mat.flags & VERTEX_COLOR) != 0)
		alpha *= dot(tri.weights, float3(tri.a.color.a, tri.b.color.a, tri.c.color.a));
	return alpha >= mat.alpha_scissor_threshold;
}

public bool trace_shadow_ray(float3 origin, float3 dir, float t_min, float t_max)
{
	RayDesc ray = { origin, t_min, dir, t_max };
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
	q.TraceRayInline(rt_tlas, RAY_FLAG_NONE, 0xff, ray);
	for (uint i = 0; i < 64 && q.Proceed(); ++i)
	{
		if (accept_candidate(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), dir, true))
		{
			q.CommitNonOpaqueTriangleHit();
			q.Abort();
			return true;
		}
	}
	return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

public bool trace_surface(float3 origin, float3 dir, float t_min, float t_max, out RTHit hit, bool include_backfaces = false, bool distance_only = false)
{
	hit = {};
	RayDesc ray = { origin, t_min, dir, t_max };
	RayQuery<RAY_FLAG_NONE> q;
	RTInstance inst;
	RTSurface s;
	Triangle tri;
	MaterialData mat;
	float3 face;
	for (uint attempt = 0; attempt < 2; ++attempt)
	{
		// A force-opaque BLAS can commit a back face before we see a candidate.
		// Retry only that case with candidate callbacks to find the next visible surface.
		q.TraceRayInline(rt_tlas, attempt == 0 ? RAY_FLAG_NONE : RAY_FLAG_FORCE_NON_OPAQUE, 0xff, ray);
		for (uint i = 0; i < 64 && q.Proceed(); ++i)
		{
			if (accept_candidate(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), dir, false, include_backfaces))
				q.CommitNonOpaqueTriangleHit();
		}
		if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
			return false;
		inst = rt_instances[q.CommittedInstanceID()];
		s = rt_surfaces[inst.first_surface + q.CommittedGeometryIndex()];
		tri = load_triangle(s, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
		face = tri.face_normal(inst);
		mat = rt_materials[s.material_id];
		if (include_backfaces || (mat.flags & DOUBLE_SIDED) != 0 || ((dot(face, dir) < 0) != ((mat.flags & CULL_FRONT) != 0)))
			break;
	}
	hit.t = q.CommittedRayT();
	hit.is_front_face = dot(face, dir) < 0;
	hit.is_double_sided = (mat.flags & DOUBLE_SIDED) != 0;
	if (distance_only || (include_backfaces && !hit.is_front_face && !hit.is_double_sided))
		return true;

	float3 w = tri.weights;
	RTVertex a = tri.a, b = tri.b, c = tri.c;
	RTMaterialTextures maps = rt_material_textures[s.material_id];
	float2 uv = tri.uv(mat);
	float3 local = a.position + (b.position - a.position) * w.y + (c.position - a.position) * w.z;
	hit.world_pos = mul_3x4(inst.model, float4(local, 1));
	hit.geometric_normal = hit.is_front_face ? face : -face;
	hit.is_unlit = (mat.flags & UNLIT) != 0;
	float3 n = normalize_or(mul_normal_3x3(inst.normal, a.normal * w.x + b.normal * w.y + c.normal * w.z), face);
	float4 tangent = a.tangent * w.x + b.tangent * w.y + c.tangent * w.z;
	if ((mat.flags & HAS_NORMAL_MAP) != 0 && abs(tangent.w) > 0.0001)
	{
		float3 t = mul_normal_3x3(inst.model, tangent.xyz);
		t = normalize_or(t - n * dot(t, n), 0);
		float sign_b = (inst.flags & 1) != 0 ? -sign(tangent.w) : sign(tangent.w);
		float2 xy = sample_map(maps.normal, uv).rg * 2 - 1;
		float z = sqrt(saturate(1 - dot(xy, xy)));
		n = normalize_or(n * z + (t * xy.x + cross(n, t) * sign_b * xy.y) * mat.normal_scale, n);
	}
	hit.shading_normal = !hit.is_front_face && (hit.is_double_sided || (mat.flags & CULL_FRONT) != 0) ? -n : n;
	float3 base = mat.albedo.rgb * sample_map(maps.albedo, uv).rgb;
	if ((mat.flags & VERTEX_COLOR) != 0)
	{
		bool srgb = (mat.flags & SRGB_VERTEX_COLOR) != 0;
		base *= (srgb ? srgb_to_linear(a.color.rgb) : a.color.rgb) * w.x
			+ (srgb ? srgb_to_linear(b.color.rgb) : b.color.rgb) * w.y
			+ (srgb ? srgb_to_linear(c.color.rgb) : c.color.rgb) * w.z;
	}
	hit.base_color = saturate(base);
	hit.roughness = saturate(mat.roughness * dot(mat.roughness_channel, sample_map(maps.roughness, uv)));
	hit.metallic = saturate(mat.metallic * dot(mat.metallic_channel, sample_map(maps.metallic, uv)));
	hit.specular = saturate(mat.specular);
	hit.emissive = mat.emission;
	if ((mat.flags & HAS_EMISSION_TEXTURE) != 0)
	{
		float3 emission = sample_map(maps.emissive, uv).rgb;
		hit.emissive = (mat.flags & EMISSION_OP_MULTIPLY) != 0 ? hit.emissive * emission : hit.emissive + emission;
	}
	hit.emissive *= mat.emission_energy;
	return true;
}
