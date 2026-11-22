import gbuffer;
import surface;

struct DrawData
{
	uint draw_id;
};
[[vk::push_constant]] ConstantBuffer<DrawData> draw;

[shader("vertex")]
VertexOutput vertex_main(VertexInput input)
{
	return transform_vertex(input, draw.draw_id);
}

[shader("fragment")]
GBufferTargets fragment_main(VertexOutput input, bool is_front_face : SV_IsFrontFace)
{
	float alpha;
	float3 emissive;
	GBufferData surface = sample_surface(input, is_front_face, false, alpha, emissive);
	return encode_gbuffer(surface, emissive);
}
