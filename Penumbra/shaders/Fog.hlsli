#ifndef PENUMBRA_FOG_HLSLI
#define PENUMBRA_FOG_HLSLI

// Fog density and how the scene is seen through it. The density mirrors
// Penumbra/src/Penumbra/Scene/FogUtils.h (perception uses the same numbers): change both or neither.
// Needs g_View (ViewBindings.hlsli or PostCommon.hlsli) declared before it.

// --- Density (FogMedium in FogUtils.h) ------------------------------------------------------------

float FogGlobalDensity(float y)
{
	if (g_View.FogHeightFalloff <= 0.0 || y <= g_View.FogHeight)
		return g_View.FogDensity;
	return g_View.FogDensity * exp(-g_View.FogHeightFalloff * (y - g_View.FogHeight));
}

float FogHeightProfileIntegral(float y)
{
	if (y <= g_View.FogHeight)
		return y;
	return g_View.FogHeight + (1.0 - exp(-g_View.FogHeightFalloff * (y - g_View.FogHeight))) / g_View.FogHeightFalloff;
}

// The environment's fog along a segment (FogMedium::OpticalDepth without the boxes).
float FogGlobalOpticalDepth(float3 from, float3 to)
{
	float3 delta = to - from;
	float len = length(delta);
	if (g_View.FogHeightFalloff <= 0.0 || g_View.FogDensity <= 0.0)
		return g_View.FogDensity * len;
	if (abs(delta.y) < 1e-4)
		return FogGlobalDensity(from.y) * len;
	return g_View.FogDensity * len * (FogHeightProfileIntegral(to.y) - FogHeightProfileIntegral(from.y)) / delta.y;
}

// FogBox::Weight: local is the point in box space.
float FogBoxWeight(float3 local, float3 halfExtents, float edgeFade)
{
	float3 inside = halfExtents - abs(local);
	float depth = min(inside.x, min(inside.y, inside.z));
	if (depth <= 0.0)
		return 0.0;
	return edgeFade <= 0.0 ? 1.0 : saturate(depth / edgeFade);
}

// --- Light (FogPhase in FogUtils.h) ------------------------------------------------------------------

// Henyey-Greenstein, scaled so scattering the same way in every direction gives 1.
float FogPhase(float cosTheta, float g)
{
	float g2 = g * g;
	return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5);
}

// --- The froxel grid --------------------------------------------------------------------------------
// Slices are spaced quadratically in view depth: slice coordinate w in [0, 1] sits at FogDistance * w^2.

float FogSliceDepth(float w)
{
	return g_View.FogDistance * w * w;
}

float FogSliceCoordinate(float viewDepth)
{
	return sqrt(saturate(viewDepth / max(g_View.FogDistance, 1e-3)));
}

// The scene colour of a point seen through the fog. volume holds, per froxel, the light scattered in
// (rgb) and the transmittance (a) from the eye to the froxel's far end (FogIntegrate.hlsl). Past the
// grid, and when there is no volume this frame, the environment's fog is applied analytically.
float3 ApplyFog(float3 color, float3 worldPosition, float2 screenUV, float viewDepth, Texture3D<float4> volume, SamplerState linearClamp)
{
	if (g_View.VolumetricFog < 0.5)
	{
		float transmittance = exp(-FogGlobalOpticalDepth(g_View.CameraPosition, worldPosition));
		return color * transmittance + g_View.FogColor * (1.0 - transmittance);
	}

	if (viewDepth > g_View.FogDistance)
	{
		float3 gridEnd = g_View.CameraPosition + (worldPosition - g_View.CameraPosition) * (g_View.FogDistance / viewDepth);
		float beyond = exp(-FogGlobalOpticalDepth(gridEnd, worldPosition));
		color = color * beyond + g_View.FogColor * (1.0 - beyond);
	}
	uint width, height, depth;
	volume.GetDimensions(width, height, depth);
	// Texel k holds the value at the far end of slice k.
	float slice = FogSliceCoordinate(viewDepth) * depth - 1.0;
	float4 fog = volume.SampleLevel(linearClamp, float3(screenUV, (slice + 0.5) / depth), 0);
	if (slice < 0.0)
		fog = lerp(float4(0.0, 0.0, 0.0, 1.0), fog, saturate(slice + 1.0));
	return color * fog.a + fog.rgb;
}

#endif
