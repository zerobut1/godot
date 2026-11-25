import common;
import gbuffer;
import surface;

[[vk::constant_id(0)]] const bool scissor = false;

struct DrawData
{
	uint draw_id;
};
[[vk::push_constant]] ConstantBuffer<DrawData> draw;

[shader("vertex")]
VertexOutput vertex_main(VertexInput input)
{
	return transform_vertex(input, draw.draw_id, scissor);
}

[shader("fragment")]
[earlydepthstencil]
GBufferTargets fragment_main(VertexOutput input, bool is_front_face : SV_IsFrontFace)
{
	return encode_gbuffer(sample_surface(input, is_front_face, false, scissor), motion_vector(input.curr_clip, input.prev_clip));
}
