import common;
import rt_reflection_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(2, 0)]] Texture2D<float> scene_depth;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> raw_reflection;
[[vk::binding(4, 0)]] Texture2D<float4> gbuffer_a;
[[vk::binding(5, 0)]] Texture2D<float4> gbuffer_c;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint> ray_list;
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> indirect_args;
[[vk::binding(8, 0)]] [[vk::image_format("r8ui")]] RWTexture2D<uint> quad_offset;

struct ClassifyParams
{
	int2 full_size;
	uint pixel_scale;
	float max_roughness;
	uint has_tlas, dispatch_rows, frame_index;
};
[[vk::push_constant]] ConstantBuffer<ClassifyParams> params;

groupshared uint group_count;
groupshared uint group_base;

bool classify_texel(int2 texel, out uint chosen)
{
	chosen = 0;
	if (params.has_tlas == 0)
		return false;
	int2 origin = int2(frame.viewport_origin);
	if (params.pixel_scale == 1)
	{
		int3 coord = int3(texel + origin, 0);
		return scene_depth.Load(coord) > 0 && gbuffer_a.Load(coord).a != 0 && gbuffer_c.Load(coord).r < params.max_roughness;
	}

	int2 base = texel * 2;
	uint active_mask = 0;
	bool mirror = false;
	float best_roughness = params.max_roughness;
	[unroll]
	for (uint i = 0; i < 4; ++i)
	{
		int2 p = base + int2(i & 1u, i >> 1);
		if (any(p >= params.full_size))
			continue;
		int3 coord = int3(p + origin, 0);
		if (scene_depth.Load(coord) <= 0 || gbuffer_a.Load(coord).a == 0)
			continue;
		float r = gbuffer_c.Load(coord).r;
		mirror = mirror || r < MIRROR_ROUGHNESS;
		if (r < params.max_roughness)
			active_mask |= 1u << i;
		if (r < best_roughness)
		{
			best_roughness = r;
			chosen = i;
		}
	}

	if (mirror)
	{
		int2 offset = reflection_pixel(texel, 2, params.frame_index, params.full_size) - base;
		chosen = uint(offset.x | (offset.y << 1));
	}
	else if ((active_mask & 1u) != 0)
	{
		// Anchor glossy quads to the lattice origin whenever it can be traced.
		chosen = 0;
	}
	return (active_mask & (1u << chosen)) != 0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 tid : SV_DispatchThreadID, uint lane : SV_GroupIndex)
{
	if (lane == 0)
		group_count = 0;
	GroupMemoryBarrierWithGroupSync();

	uint width, height;
	raw_reflection.GetDimensions(width, height);
	bool active = false;
	uint offset = 0;
	if (all(tid.xy < uint2(width, height)))
	{
		uint chosen;
		active = classify_texel(int2(tid.xy), chosen);
		quad_offset[tid.xy] = chosen;
		if (active)
			InterlockedAdd(group_count, 1, offset);
		else
			raw_reflection[tid.xy] = float4(0, 0, 0, -1);
	}
	GroupMemoryBarrierWithGroupSync();

	if (lane == 0 && group_count != 0)
	{
		uint base;
		InterlockedAdd(ray_list[0], group_count, base);
		group_base = base + 1;
		// The last reservation ends at the total count, independent of execution order.
		uint rays_per_column = 64 * params.dispatch_rows;
		InterlockedMax(indirect_args[0], (base + group_count + rays_per_column - 1) / rays_per_column);
	}
	GroupMemoryBarrierWithGroupSync();
	if (active)
		ray_list[group_base + offset] = tid.x | (tid.y << 16);
}
