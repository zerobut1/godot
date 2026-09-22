#[vertex]

#VERSION_DEFINES

struct VertexOutput
{
	float4 position : SV_Position;
	float3 color : COLOR0;
};

VertexOutput main(uint vertex_id : SV_VertexID)
{
	VertexOutput output;

	if (vertex_id == 0)
	{
		output.position = float4(0.0, -0.65, 0.0, 1.0);
		output.color = float3(1.0, 0.15, 0.1);
	}
	else if (vertex_id == 1)
	{
		output.position = float4(-0.65, 0.55, 0.0, 1.0);
		output.color = float3(0.1, 1.0, 0.2);
	}
	else
	{
		output.position = float4(0.65, 0.55, 0.0, 1.0);
		output.color = float3(0.15, 0.3, 1.0);
	}

	return output;
}

#[fragment]

#VERSION_DEFINES

struct VertexOutput
{
	float4 position : SV_Position;
	float3 color : COLOR0;
};

float4 main(VertexOutput input) : SV_Target0
{
	return float4(input.color, 1.0);
}
