#include "Common.hlsli"

// =============================================================================================
// Volumetric fog, step 2 of 2: walks every pixel column of the froxel grid from the eye outwards and
// sums what the fog adds (light scattered in) and takes away (transmittance) up to each slice's far
// end. Mesh and sky shaders then read one texel per pixel (Fog.hlsli, ApplyFog).
// Own binding layout (no view set): the view set reads the output as a texture.
// =============================================================================================

struct IntegratePush
{
	float4 Params0; // x fog distance (the grid's far end, view depth), y tan(half fov x), z tan(half fov y)
	float4 Params1;
	float4 Params2;
};

PN_PUSH_CONSTANT ConstantBuffer<IntegratePush> g_Integrate : register(b0);
Texture3D<float4> g_Scatter : register(t0);
[[vk::image_format("rgba16f")]] RWTexture3D<float4> g_Integrated : register(u0);

[numthreads(8, 8, 1)]
void CSFogIntegrate(uint3 id : SV_DispatchThreadID)
{
	uint width, height, depth;
	g_Integrated.GetDimensions(width, height, depth);
	if (id.x >= width || id.y >= height)
		return;

	// View depth to distance along this column's ray.
	float2 ndc = (float2(id.xy) + 0.5) / float2(width, height) * float2(2.0, -2.0) + float2(-1.0, 1.0);
	float stretch = length(float3(ndc.x * g_Integrate.Params0.y, ndc.y * g_Integrate.Params0.z, 1.0));
	float far = g_Integrate.Params0.x;

	float3 scattered = 0.0;
	float transmittance = 1.0;
	float previousDepth = 0.0;
	for (uint slice = 0; slice < depth; slice++)
	{
		float w = (slice + 1.0) / depth;
		float sliceDepth = far * w * w;
		float stepLength = (sliceDepth - previousDepth) * stretch;
		previousDepth = sliceDepth;

		float4 froxel = g_Scatter[uint3(id.xy, slice)];
		float extinction = max(froxel.a, 1e-6);
		float sliceTransmittance = exp(-extinction * stepLength);
		// Energy-conserving integration of the light scattered in over the slice.
		scattered += transmittance * (froxel.rgb - froxel.rgb * sliceTransmittance) / extinction;
		transmittance *= sliceTransmittance;
		g_Integrated[uint3(id.xy, slice)] = float4(scattered, transmittance);
	}
}
