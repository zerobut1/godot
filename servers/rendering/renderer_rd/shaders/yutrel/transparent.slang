import common;
import transparent_csm;
import surface;
import transparent_lighting;

[shader("vertex")]
VertexOutput vertex_main(VertexInput input)
{
	return transparent_vertex(input);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input, bool is_front_face : SV_IsFrontFace) : SV_Target0
{
	float3 world = reconstruct_world_position(input.position.xy, input.position.z, frame);
	CascadeShadows shadows = { dot(frame.camera_position.xyz - world, frame.camera_forward.xyz), floor(input.position.xy) };
	return shade_transparent(input, is_front_face, world, shadows);
}
