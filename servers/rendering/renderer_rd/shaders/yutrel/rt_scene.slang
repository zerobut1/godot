import common;
import surface_evaluation;

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
	uint slots[6];
	uint2 padding;
};

// Compute passes use set 1. Forward geometry reserves that set for its material.
#ifndef YUTREL_RT_SCENE_SET
#define YUTREL_RT_SCENE_SET 1
#endif

[[vk::binding(0, YUTREL_RT_SCENE_SET)]] RaytracingAccelerationStructure rt_tlas;
[[vk::binding(1, YUTREL_RT_SCENE_SET)]] StructuredBuffer<RTVertex> rt_vertices;
[[vk::binding(2, YUTREL_RT_SCENE_SET)]] StructuredBuffer<uint> rt_indices;
[[vk::binding(3, YUTREL_RT_SCENE_SET)]] StructuredBuffer<RTInstance> rt_instances;
[[vk::binding(4, YUTREL_RT_SCENE_SET)]] StructuredBuffer<RTSurface> rt_surfaces;
[[vk::binding(5, YUTREL_RT_SCENE_SET)]] StructuredBuffer<MaterialData> rt_materials;
[[vk::binding(6, YUTREL_RT_SCENE_SET)]] StructuredBuffer<RTMaterialTextures> rt_material_textures;
[[vk::binding(7, YUTREL_RT_SCENE_SET)]] SamplerState rt_sampler;
[[vk::binding(8, YUTREL_RT_SCENE_SET)]] Texture2D<float4> rt_textures[2048];

public struct RTHit
{
	public SurfaceData surface;
	public float3 world_pos, geometric_normal;
	public float t;
	public uint blend_mode;
	public bool is_front_face, is_double_sided, transparent;
};

public float3 offset_ray_origin(float3 p, float3 n)
{
	// Wächter and Binder, Ray Tracing Gems, chapter 6.
	int3 steps = int3(256.0 * n);
	float3 offset = asfloat(asint(p) + select(p < 0, -steps, steps));
	return select(abs(p) < 1.0 / 32.0, p + n / 65536.0, offset);
}

float4 sample_map(uint index, float2 uv, float mip_lod)
{
	return rt_textures[NonUniformResourceIndex(index)].SampleLevel(rt_sampler, uv, mip_lod);
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

	float3 face_normal(RTInstance inst, out float double_area)
	{
		// Godot triangles wind clockwise. Restore object orientation after a reflection.
		float3 ab = mul_normal_3x3(inst.model, b.position - a.position);
		float3 ac = mul_normal_3x3(inst.model, c.position - a.position);
		float3 cross_product = cross(ac, ab);
		double_area = length(cross_product);
		return normalize(cross_product) * ((inst.flags & 1) != 0 ? -1.0 : 1.0);
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

// Must match YutrelRayTracingScene's instance visibility masks.
static const uint MASK_SHADOW = 1;
static const uint MASK_SURFACE = 2;
static const uint MASK_TRANSLUCENT = 4;

bool accept_candidate(uint instance, uint geometry, uint primitive, float2 bary, float3 dir, bool shadow, bool include_backfaces = false, bool include_transparent = false)
{
	RTInstance inst = rt_instances[instance];
	RTSurface s = rt_surfaces[inst.first_surface + geometry];
	if ((s.flags & 1) != 0 || (!shadow && (inst.flags & 2) != 0))
		return false;
	MaterialData mat = rt_materials[s.material_id];
	if (!include_transparent && (mat.flags & TRANSPARENT) != 0)
		return false;
	bool check_side = !include_backfaces && (mat.flags & DOUBLE_SIDED) == 0 && (!shadow || (mat.flags & CULL_FRONT) != 0);
	if (!check_side && (mat.flags & ALPHA_SCISSOR) == 0)
		return true;
	Triangle tri = load_triangle(s, primitive, bary);
	float double_area;
	if (check_side && ((dot(tri.face_normal(inst, double_area), dir) < 0) == ((mat.flags & CULL_FRONT) != 0)))
		return false;
	if ((mat.flags & ALPHA_SCISSOR) == 0)
		return true;
	float alpha = mat.albedo.a * sample_map(rt_material_textures[s.material_id].slots[uint(SurfaceTexture::Albedo)], tri.uv(mat), 0.0).a;
	if ((mat.flags & VERTEX_COLOR) != 0)
		alpha *= dot(tri.weights, float3(tri.a.color.a, tri.b.color.a, tri.c.color.a));
	return alpha >= mat.alpha_scissor_threshold;
}

public bool trace_shadow_ray(float3 origin, float3 dir, float t_min, float t_max)
{
	RayDesc ray = { origin, t_min, dir, t_max };
	RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
	q.TraceRayInline(rt_tlas, RAY_FLAG_NONE, MASK_SHADOW, ray);
	while (q.Proceed())
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

public bool trace_shadow_ray_distance(float3 origin, float3 dir, float t_min, float t_max, out float hit_t)
{
	RayDesc ray = { origin, t_min, dir, t_max };
	RayQuery<RAY_FLAG_NONE> q;
	q.TraceRayInline(rt_tlas, RAY_FLAG_NONE, MASK_SHADOW, ray);
	while (q.Proceed())
	{
		if (accept_candidate(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), dir, true))
			q.CommitNonOpaqueTriangleHit();
	}
	bool blocked = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
	hit_t = blocked ? q.CommittedRayT() : 0;
	return blocked;
}

public float3 sample_sun_disk(float3 light_dir, float tan_angular_radius, float2 u)
{
	float3 tangent = normalize(cross(abs(light_dir.z) < 0.99 ? float3(0, 0, 1) : float3(0, 1, 0), light_dir));
	float angle = 6.283185307 * u.y;
	return normalize(light_dir + tan_angular_radius * sqrt(u.x) * (cos(angle) * tangent + sin(angle) * cross(light_dir, tangent)));
}

// Log2 UV footprint from the projected cone, plus the texture-specific texel density.
float cone_mip(uint index, float uv_lod, bool use_cone)
{
	if (!use_cone)
		return 0;
	uint width, height;
	rt_textures[NonUniformResourceIndex(index)].GetDimensions(width, height);
	return clamp(uv_lod + 0.5 * log2(float(width) * height), 0, 8);
}

struct RTSurfaceMaps : ISurfaceMaps
{
	RTMaterialTextures textures;
	float2 uv;
	float uv_lod;
	bool use_cone;

	float lod(SurfaceTexture slot)
	{
		return cone_mip(textures.slots[uint(slot)], uv_lod, use_cone);
	}

	float4 sample_texture(SurfaceTexture slot)
	{
		return sample_map(textures.slots[uint(slot)], uv, lod(slot));
	}
};

public bool trace_surface(float3 origin, float3 dir, float t_min, float t_max, out RTHit hit, bool include_backfaces = false, bool distance_only = false,
		float2 ray_cone = float2(0), bool skip_normal_map = false, bool include_transparent = false)
{
	hit = {};
	RayDesc ray = { origin, t_min, dir, t_max };
	RayQuery<RAY_FLAG_NONE> q;
	RTInstance inst;
	RTSurface s;
	Triangle tri;
	MaterialData mat;
	float3 face;
	float double_area;
	uint ray_flags = RAY_FLAG_NONE;
	for (uint attempt = 0; attempt < 2; ++attempt)
	{
		// A force-opaque BLAS can commit a back face before we see a candidate.
		// Retry only that case with candidate callbacks to find the next visible surface.
		q.TraceRayInline(rt_tlas, ray_flags, include_transparent ? MASK_SURFACE | MASK_TRANSLUCENT : MASK_SURFACE, ray);
		while (q.Proceed())
		{
			if (accept_candidate(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics(), dir, false, include_backfaces, include_transparent))
				q.CommitNonOpaqueTriangleHit();
		}
		if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
			return false;
		inst = rt_instances[q.CommittedInstanceID()];
		s = rt_surfaces[inst.first_surface + q.CommittedGeometryIndex()];
		tri = load_triangle(s, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
		face = tri.face_normal(inst, double_area);
		mat = rt_materials[s.material_id];
		if (include_backfaces || (mat.flags & DOUBLE_SIDED) != 0 || ((dot(face, dir) < 0) != ((mat.flags & CULL_FRONT) != 0)))
			break;
		ray_flags = RAY_FLAG_FORCE_NON_OPAQUE;
	}
	hit.t = q.CommittedRayT();
	hit.is_front_face = dot(face, dir) < 0;
	hit.is_double_sided = (mat.flags & DOUBLE_SIDED) != 0;
	if (distance_only || (include_backfaces && !hit.is_front_face && !hit.is_double_sided))
		return true;

	float3 w = tri.weights;
	RTVertex a = tri.a, b = tri.b, c = tri.c;
	float2 uv = tri.uv(mat);
	bool use_cone = ray_cone.x > 0;
	float uv_lod = 0;
	if (use_cone)
	{
		float2 duv1 = float2(b.u - a.u, b.v - a.v) * mat.uv_transform.xy;
		float2 duv2 = float2(c.u - a.u, c.v - a.v) * mat.uv_transform.xy;
		float uv_area = abs(duv1.x * duv2.y - duv1.y * duv2.x);
		float width = (ray_cone.x + hit.t * ray_cone.y) / max(abs(dot(dir, face)), 0.1);
		uv_lod = 0.5 * log2(max(1e-30, width * width * uv_area / max(double_area, 1e-20)));
	}
	float3 local = a.position + (b.position - a.position) * w.y + (c.position - a.position) * w.z;
	hit.world_pos = mul_3x4(inst.model, float4(local, 1));
	hit.geometric_normal = hit.is_front_face ? face : -face;
	hit.transparent = (mat.flags & TRANSPARENT) != 0;
	hit.blend_mode = mat.blend_mode;
	RTSurfaceMaps maps = { rt_material_textures[s.material_id], uv, uv_lod, use_cone };
	float3 n = normalize_or(mul_normal_3x3(inst.normal, a.normal * w.x + b.normal * w.y + c.normal * w.z), face);
	float3 t = 0, binormal = 0;
	if (!skip_normal_map && (mat.flags & HAS_NORMAL_MAP) != 0)
	{
		float4 tangent = a.tangent * w.x + b.tangent * w.y + c.tangent * w.z;
		if (abs(tangent.w) > 0.0001 && maps.lod(SurfaceTexture::Normal) < 2.0)
		{
			t = mul_normal_3x3(inst.model, tangent.xyz);
			binormal = cross(n, t) * ((inst.flags & 1) != 0 ? -tangent.w : tangent.w);
		}
	}
	float4 color = 1;
	if ((mat.flags & VERTEX_COLOR) != 0)
		color = linear_vertex_color(mat, a.color) * w.x + linear_vertex_color(mat, b.color) * w.y + linear_vertex_color(mat, c.color) * w.z;
	float4 base = evaluate_base(mat, maps.sample_texture(SurfaceTexture::Albedo), color);
	hit.surface = evaluate_surface(mat, maps, base, n, t, binormal, hit.is_front_face);
	return true;
}
