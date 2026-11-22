// Inverse of Vulkan's cube lookup. Texel rows run from -1 to +1 in face V.
public float3 cube_direction(uint face, float2 uv)
{
	switch (face)
	{
		case 0: return float3( 1.0, -uv.y, -uv.x);
		case 1: return float3(-1.0, -uv.y,  uv.x);
		case 2: return float3( uv.x,  1.0,  uv.y);
		case 3: return float3( uv.x, -1.0, -uv.y);
		case 4: return float3( uv.x, -uv.y,  1.0);
		default: return float3(-uv.x, -uv.y, -1.0);
	}
}

public float3 inverse_sky_rotation(float3 v, float sin_rot, float cos_rot)
{
	return float3(cos_rot * v.x - sin_rot * v.z, v.y, sin_rot * v.x + cos_rot * v.z);
}

// Filament SH3 order. Projection and evaluation use the same real polynomial basis.
public void sh3_basis(float3 n, out float basis[9])
{
	basis[0] = 1.0;
	basis[1] = n.y;
	basis[2] = n.z;
	basis[3] = n.x;
	basis[4] = n.y * n.x;
	basis[5] = n.y * n.z;
	basis[6] = 3.0 * n.z * n.z - 1.0;
	basis[7] = n.z * n.x;
	basis[8] = n.x * n.x - n.y * n.y;
}
