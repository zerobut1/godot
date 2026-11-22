import ibl_common;

[[vk::binding(0, 0)]] TextureCube<float4> source_mip;
[[vk::binding(1, 0)]] SamplerState point_sampler;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> partial_sums;
[[vk::binding(3, 0)]] RWStructuredBuffer<float4> sh;

struct SHParams
{
	uint reduce;
	uint face_size;
};
[[vk::push_constant]] ConstantBuffer<SHParams> params;

groupshared float4 tile_sums[9][64];

float area_element(float2 corner)
{
	return atan2(corner.x * corner.y, sqrt(dot(corner, corner) + 1.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	uint tiles_per_row = params.face_size / 8;
	uint tiles_per_face = tiles_per_row * tiles_per_row;
	uint tile_count = 6 * tiles_per_face;
	bool reduce = params.reduce != 0;
	if (reduce)
	{
		float4 sum = float4(0.0);
		for (uint tile = lane; tile < tile_count; tile += 64)
		{
			sum += partial_sums[group.x * tile_count + tile];
		}
		tile_sums[0][lane] = sum;
	}
	else
	{
		float texel_width = 2.0 / params.face_size;
		float2 lo = float2(id.xy) * texel_width - 1.0;
		float2 hi = lo + texel_width;
		float d_omega = area_element(hi) - area_element(float2(lo.x, hi.y))
				- area_element(float2(hi.x, lo.y)) + area_element(lo);
		float3 dir = normalize(cube_direction(id.z, (lo + hi) * 0.5));
		float3 radiance = source_mip.SampleLevel(point_sampler, dir, 0.0).rgb;
		float basis[9];
		sh3_basis(dir, basis);
		for (uint i = 0; i < 9; ++i)
		{
			tile_sums[i][lane] = float4(radiance * (basis[i] * d_omega), d_omega);
		}
	}

	GroupMemoryBarrierWithGroupSync();
	uint coefficient_count = reduce ? 1 : 9;
	for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (lane < stride)
		{
			for (uint i = 0; i < coefficient_count; ++i)
			{
				tile_sums[i][lane] += tile_sums[i][lane + stride];
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}

	if (lane == 0)
	{
		if (reduce)
		{
			// 4*pi times the squared SH normalization constants and the Lambert/PI
			// band factors (1, 2/3, 1/4). The measured solid angle normalizes the sum.
			const float scale[9] = { 1.0, 2.0, 2.0, 2.0, 3.75, 3.75, 0.3125, 3.75, 0.9375 };
			float4 sum = tile_sums[0][0];
			sh[group.x] = float4(sum.rgb / sum.w * scale[group.x], 0.0);
		}
		else
		{
			uint tile = group.z * tiles_per_face + group.y * tiles_per_row + group.x;
			for (uint i = 0; i < 9; ++i)
			{
				partial_sums[i * tile_count + tile] = tile_sums[i][0];
			}
		}
	}
}
