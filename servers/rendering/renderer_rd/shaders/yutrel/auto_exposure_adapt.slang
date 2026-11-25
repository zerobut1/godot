struct AdaptParams {
	float min_ev, max_ev, compensation, speed_up, speed_down, dt;
	uint reset;
};
[[vk::push_constant]] ConstantBuffer<AdaptParams> params;
[[vk::binding(0, 0)]] StructuredBuffer<uint> histogram;
[[vk::binding(1, 0)]] [[vk::image_format("rg32f")]] RWTexture2D<float2> exposure;
groupshared uint cumulative[256];
groupshared float weighted_bins[256];

[shader("compute")]
[numthreads(256, 1, 1)]
void compute_main(uint lane : SV_GroupIndex)
{
	uint weight = lane == 0 ? 0 : histogram[lane];
	cumulative[lane] = weight;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = 1; stride < 256; stride *= 2)
	{
		uint add = lane >= stride ? cumulative[lane - stride] : 0;
		GroupMemoryBarrierWithGroupSync();
		cumulative[lane] += add;
		GroupMemoryBarrierWithGroupSync();
	}

	float total = cumulative[255];
	float lo = total * 0.5, hi = total * 0.95;
	float overlap = max(0.0, min(float(cumulative[lane]), hi) - max(float(cumulative[lane] - weight), lo));
	weighted_bins[lane] = float(lane) * overlap;
	GroupMemoryBarrierWithGroupSync();
	for (uint stride = 128; stride > 0; stride /= 2)
	{
		if (lane < stride)
			weighted_bins[lane] += weighted_bins[lane + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (lane == 0)
	{
		float avg_bin = total > 0 ? weighted_bins[0] / (hi - lo) : 1.0;
		float luma = exp2(-12.0 + (avg_bin - 1.0) * (24.0 / 254.0));
		float ev = clamp(log2(0.18 / max(luma, 1e-4)) + params.compensation, min(params.min_ev, params.max_ev), max(params.min_ev, params.max_ev));
		if (params.reset == 0)
		{
			float prev = exposure[int2(0, 0)].g;
			float speed = ev < prev ? params.speed_up : params.speed_down;
			float dt = params.dt > 0 ? min(params.dt, 0.25) : 1.0 / 60.0;
			ev = lerp(prev, ev, 1.0 - exp(-dt * speed));
		}
		exposure[int2(0, 0)] = float2(max(exp2(ev), 1e-4), ev);
	}
}
