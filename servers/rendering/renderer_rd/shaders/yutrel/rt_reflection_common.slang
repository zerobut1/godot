// Adapted from FidelityFX Reflections Denoiser and Temporal Reprojection Anti-Aliasing.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Copyright (c) 2015 Playdead.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

import common;

public static const float MIRROR_ROUGHNESS = 0.04;

public int2 reflection_phase(uint index, uint scale)
{
	int2 phases[4] = { int2(0, 0), int2(1, 1), int2(1, 0), int2(0, 1) };
	return scale == 1 ? int2(0) : phases[index & 3];
}

public int2 reflection_pixel(int2 texel, uint scale, uint index, int2 full_size)
{
	return min(texel * int(scale) + reflection_phase(index, scale), full_size - 1);
}

// Invert the jittered lattice, including the one-pixel interval at an odd edge.
public float2 reflection_texel(float2 pixel, uint scale, float2 phase, int2 size)
{
	float2 p = (pixel - phase) / float(scale);
	int2 last = (size - 1) / int(scale);
	float2 end = min(last * int(scale) + phase, float2(size - 1));
	float2 interval = end - ((last - 1) * int(scale) + phase);
	return select((last > 0) && (p > last - 1), last + (pixel - end) / interval, p);
}

public float3 reflection_normal(float4 packed)
{
	return normalize_or(packed.rgb * 2 - 1, float3(0, 0, 1));
}

public float3 reflection_view(float3 world, FrameData frame)
{
	return frame.camera_forward.w != 0 ? frame.camera_forward.xyz : normalize_or(frame.camera_position.xyz - world, frame.camera_forward.xyz);
}

public float linear_view_depth(float3 world, FrameData frame)
{
	return max(1e-5, dot(frame.camera_position.xyz - world, frame.camera_forward.xyz));
}

// XY is an octahedral unit normal, Z is positive linear view depth (zero is
// invalid), and W is perceptual roughness. History guides are viewport-local.
public float4 reflection_guide(float3 N, float depth, float r)
{
	float2 oct = N.xy / dot(abs(N), float3(1));
	if (N.z < 0)
		oct = (1 - abs(oct.yx)) * select(oct >= 0, 1.0, -1.0);
	return float4(oct * 0.5 + 0.5, min(depth, 65504.0), r);
}

public float2 previous_pixel(float3 world, FrameData prev)
{
	float4 clip = mul_4x4(prev.view_projection, float4(world, 1));
	return clip.w > 0 ? (clip.xy / clip.w * 0.5 + 0.5) / prev.inv_viewport_size - 0.5 : float2(-1e6);
}

public bool use_virtual_hit(float3 N, float r, float4 hit, float4 surface)
{
	float hit_similarity = dot(N, unpack_octahedral_normal(hit.xy));
	float surface_similarity = dot(N, unpack_octahedral_normal(surface.xy));
	return hit.z > 0 && hit_similarity > 0.9999 && hit_similarity + 0.001 > surface_similarity
			&& abs(hit.w - r) < abs(surface.w - r) + 0.001;
}

public float disocclusion(float3 N, float depth, float4 guide)
{
	float similarity = saturate(dot(N, unpack_octahedral_normal(guide.xy)));
	return exp(-1.4 * (1 - similarity) - abs(guide.z - depth) / depth);
}

public float normal_sigma(float r)
{
	return lerp(24.0, 4.0, smoothstep(MIRROR_ROUGHNESS, 0.5, r));
}

public float reflection_luma(float3 c)
{
	return max(dot(c, float3(0.299, 0.587, 0.114)), 0.001);
}

public float temporal_variance(float3 a, float3 b)
{
	float x = reflection_luma(a), y = reflection_luma(b);
	float d = (x - y) / max(max(x, y), 0.5);
	return d * d;
}

public float3 clip_radiance(float3 center, float3 extent, float3 value)
{
	float3 delta = value - center;
	float3 unit = abs(delta) / (extent + 0.001);
	return center + delta / max(1.0, max(unit.x, max(unit.y, unit.z)));
}

public float3 sample_tile_average(Texture2D<float4> average, SamplerState linear_clamp, int2 pixel, int2 work_size)
{
	float2 uv = (pixel + 0.5) / (8 * ((work_size + 7) / 8));
	float4 value = average.SampleLevel(linear_clamp, uv, 0);
	return value.rgb / max(value.a, 1e-5);
}
