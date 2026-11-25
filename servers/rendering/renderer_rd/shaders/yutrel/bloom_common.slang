import common;

// Jimenez's 13 bilinear taps form five overlapping quads. Keep the group
// means separate until the optional first-level Karis normalization.
public float3 bloom_downfilter(Texture2D<float4> source, SamplerState sampler_state, float2 center,
		float2 texel, float2 uv_min, float2 uv_max, bool prefilter, float exposure, float threshold, float soft_knee)
{
	const int2 offsets[13] = {
		int2(-2,-2), int2(0,-2), int2(2,-2), int2(-2,0), int2(0,0), int2(2,0), int2(-2,2), int2(0,2), int2(2,2),
		int2(-1,-1), int2(1,-1), int2(-1,1), int2(1,1)
	};
	float3 taps[13];
	[unroll] for (int i = 0; i < 13; ++i)
	{
		float2 uv = clamp(center + offsets[i] * texel, uv_min, uv_max);
		float3 c = source.SampleLevel(sampler_state, uv, 0).rgb;
		if (prefilter)
		{
			c = clamp(max(sanitize_finite(c), 0.0) * exposure, 0.0, 65504.0);
			float br = max(c.r, max(c.g, c.b));
			float knee = threshold * soft_knee + 1e-5;
			float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
			soft = soft * soft / (4.0 * knee);
			c *= max(soft, br - threshold) / max(br, 1e-5);
		}
		taps[i] = c;
	}
	float3 groups[5] = {
		(taps[0]+taps[1]+taps[3]+taps[4])*0.25, (taps[1]+taps[2]+taps[4]+taps[5])*0.25,
		(taps[3]+taps[4]+taps[6]+taps[7])*0.25, (taps[4]+taps[5]+taps[7]+taps[8])*0.25,
		(taps[9]+taps[10]+taps[11]+taps[12])*0.25
	};
	float3 sum = 0;
	float weight_sum = 0;
	[unroll] for (int i = 0; i < 5; ++i)
	{
		float w = i == 4 ? 0.5 : 0.125;
		if (prefilter)
			w /= 1.0 + max(dot(groups[i], float3(0.2126, 0.7152, 0.0722)), 0.0);
		sum += groups[i] * w;
		weight_sum += w;
	}
	return sum / weight_sum;
}
