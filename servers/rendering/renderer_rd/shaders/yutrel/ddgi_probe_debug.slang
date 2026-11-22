import brdf;
import common;
import ddgi_common;
import output_common;

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
[[vk::binding(1, 0)]] ConstantBuffer<DDGIVolumeData> volume;
[[vk::binding(2, 0)]] Texture2DArray<float4> irradiance;
[[vk::binding(3, 0)]] SamplerState linear_clamp;
[[vk::binding(4, 0)]] Texture2DArray<float4> probe_data;
[[vk::push_constant]] ConstantBuffer<OutputData> output_data;

struct Vertex
{
	float4 position : SV_Position;
	float3 normal;
	nointerpolation int3 grid;
	nointerpolation uint is_active;
};

[shader("vertex")]
Vertex vertex_main(uint vertex : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
	// Twelve meridians and eight latitude bands, two triangles per patch.
	static const uint2 corners[6] = { {0, 0}, {0, 1}, {1, 1}, {0, 0}, {1, 1}, {1, 0} };
	uint patch = vertex / 6;
	float2 uv = float2(uint2(patch % 12, patch / 12) + corners[vertex % 6]) / float2(12, 8);
	float phi = uv.x * 2 * PI, theta = uv.y * PI;
	Vertex o;
	o.normal = float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
	o.grid = ddgi_probe_grid(instance, volume);
	o.is_active = ddgi_probe_is_active(o.grid, volume, probe_data) ? 1 : 0;
	float radius = volume.probe_preview_radius * (o.is_active != 0 ? 1.0 : 0.45);
	float3 pos = ddgi_probe_world_position(o.grid, volume, probe_data) + radius * o.normal;
	o.position = mul_4x4(frame.view_projection, float4(pos, 1));
	return o;
}

[shader("fragment")]
float4 fragment_main(Vertex v) : SV_Target0
{
	if (v.is_active == 0)
		return float4(output_transform(float3(0.45, 0.04, 0.06), output_data), 1);
	float3 uv = ddgi_get_probe_uv(v.grid, ddgi_oct_encode(normalize(v.normal)), 6, volume);
	float3 linear = 2 * pow(max(irradiance.SampleLevel(linear_clamp, uv, 0).rgb, 0), volume.irradiance_gamma);
	return float4(output_transform(linear, output_data), 1);
}
