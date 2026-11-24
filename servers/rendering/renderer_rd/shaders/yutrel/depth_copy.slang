import common;

[[vk::binding(0)]] Texture2D<float> scene_depth;
struct DepthCopyParams
{
	float2 scale;
	float2 jitter_px;
};
[[vk::push_constant]] ConstantBuffer<DepthCopyParams> params;

[shader("vertex")]
float4 vertex_main(uint vertex_id : SV_VulkanVertexID) : SV_Position
{
	return fullscreen_position(vertex_id);
}

[shader("fragment")]
float fragment_main(float4 position : SV_Position) : SV_Depth
{
	uint width, height;
	scene_depth.GetDimensions(width, height);
	int2 pixel = clamp(int2(floor(position.xy * params.scale + params.jitter_px)), 0, int2(width, height) - 1);
	return scene_depth.Load(int3(pixel, 0));
}
