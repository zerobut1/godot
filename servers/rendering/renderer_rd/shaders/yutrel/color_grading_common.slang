import common;
import output_common;

public static const uint LUT_SIZE = 32;
static const float LOG_EPS = 0.000244140625;
static const float3 REC709 = float3(0.2126, 0.7152, 0.0722);

public struct GradingData
{
	public float temperature, tint, contrast, saturation;
	public float4 shadows, midtones, highlights;
	public uint tone_mapping;
	public float strip_size, strip_intensity;
	public uint has_grading;
};

public struct OutputUniforms
{
	public GradingData grade;
	public uint output_linear, use_auto_exposure, use_lut_3d, debanding;
	public float exposure, bloom_intensity, vignette_intensity, vignette_smoothness;
	public float2 origin, inv_size;
};

public float3 linear_to_lut_uvw(float3 c)
{
	float3 u = saturate((log2(max(c, 0.0) + LOG_EPS) + 12.0) / 22.0);
	return (u * (LUT_SIZE - 1) + 0.5) / LUT_SIZE;
}

public float3 lut_u_to_linear(float3 u)
{
	return max(exp2(u * 22.0 - 12.0) - LOG_EPS, 0.0);
}

public float3 apply_color_grading(float3 c, GradingData g)
{
	if (g.temperature != 0 || g.tint != 0)
	{
		// Linear Rec.709 -> CIE XYZ (D65) -> Bradford cone responses.
		const float3x3 rgb_to_lms = float3x3(
			0.42272529, 0.49134532, 0.02735794,
			0.05569978, 0.96153405, 0.02318381,
			0.02138264, 0.08764187, 0.98050813);
		const float3x3 lms_to_rgb = float3x3(
			2.53804452, -1.29327700, -0.04023688,
			-0.14600406, 1.11664831, -0.02232903,
			-0.04229851, -0.07160722, 1.02275269);
		float3 scale = exp2(float3(0.30 * g.temperature + 0.08 * g.tint,
				-0.18 * g.tint, -0.50 * g.temperature + 0.12 * g.tint));
		c = max(mul(lms_to_rgb, mul(rgb_to_lms, c) * scale), 0.0);
	}
	if (g.contrast != 1.0)
		c = exp2((log2(max(c, 1e-5)) - log2(0.18)) * g.contrast + log2(0.18));

	float luma = dot(c, REC709);
	float ws = 1.0 - smoothstep(0.0, 0.35, luma);
	float wh = smoothstep(0.55, 1.5, luma);
	float wm = max(0.0, 1.0 - ws - wh);
	c *= (g.shadows.rgb * ws + g.midtones.rgb * wm + g.highlights.rgb * wh) / max(ws + wm + wh, 1e-5);
	return max(lerp(dot(c, REC709), c, g.saturation), 0.0);
}

public float3 apply_tonemap(float3 c, uint mode)
{
	if (mode < 2) return tonemap_agx(c, mode == 1);
	if (mode == 2) return tonemap_aces(c);
	return c;
}

public float3 apply_strip(float3 c, GradingData g, Texture2D<float4> strip, SamplerState sampler_state)
{
	if (g.strip_size < 2 || g.strip_intensity == 0) return c;
	float n = g.strip_size;
	float3 cell = linear_to_srgb(saturate(c)) * (n - 1.0);
	float slice = floor(cell.b);
	float2 uv = (cell.rg + 0.5) / float2(n * n, n);
	float3 a = strip.SampleLevel(sampler_state, uv + float2(slice / n, 0), 0).rgb;
	float3 b = strip.SampleLevel(sampler_state, uv + float2(min(slice + 1.0, n - 1.0) / n, 0), 0).rgb;
	// TextureStorage supplies the sRGB-decoding view for encoded color textures.
	return lerp(c, lerp(a, b, cell.b - slice), g.strip_intensity);
}
