#include "Lighting.hlsli"

// =============================================================================================
// Volumetric fog, step 1 of 2: what each froxel of the view frustum holds. A froxel's density comes
// from the environment (height fog) and the fog boxes (Scene/FogUtils.h); the light it sends towards
// the eye is the fog's own glow (FogColor) plus FogScattering of every lamp's light reaching it
// (shadowed where the lamp has a shadow map), weighted by a Henyey-Greenstein phase function.
// Output per froxel: rgb = density * radiance scattered towards the eye, a = density. Samples are
// jittered in depth every frame and blended with last frame's (reprojected), so slices do not band.
// =============================================================================================

struct FogPush
{
	float4 Params0; // x history weight (0 = no history), y unshadowed lamps' share, zw unused
	float4 Params1;
	float4 Params2;
	float4 Params3; // GPU::PostConstants is 64 bytes
};

struct GPUFogVolume
{
	float4 CenterDensity;
	float4 HalfExtentsFade;
	float4 Row0;
	float4 Row1;
	float4 Row2;
};

PN_PUSH_CONSTANT ConstantBuffer<FogPush> g_FogPush : register(b0, space1);
Texture3D<float4> g_FogHistory : register(t0, space1);
StructuredBuffer<GPUFogVolume> g_FogVolumes : register(t1, space1);
PN_IMAGE_FORMAT_RGBA16F RWTexture3D<float4> g_FogScatter : register(u0, space1);

// FogMedium::DensityAt.
float FogDensityAt(float3 position)
{
	float density = FogGlobalDensity(position.y);
	for (uint i = 0; i < g_View.FogVolumeCount; i++)
	{
		GPUFogVolume volume = g_FogVolumes[i];
		float3 offset = position - volume.CenterDensity.xyz;
		float3 local = float3(dot(volume.Row0.xyz, offset), dot(volume.Row1.xyz, offset), dot(volume.Row2.xyz, offset));
		density += volume.CenterDensity.w * FogBoxWeight(local, volume.HalfExtentsFade.xyz, volume.HalfExtentsFade.w);
	}
	return density;
}

// One-tap shadow lookups: the jitter and the temporal blend soften them.
float FogDirectionalShadow(float3 position, float viewDepth)
{
	if (g_View.DirectionalShadowIndex < 0.0)
		return 1.0;
	uint cascade = 0;
	[unroll] for (uint c = 0; c < MaxCascades - 1; c++)
	{
		if (viewDepth > g_View.CascadeSplits[c] && c + 1 < (uint)g_View.CascadeCount)
			cascade = c + 1;
	}
	if (viewDepth > g_View.CascadeSplits[(uint)g_View.CascadeCount - 1])
		return 1.0;
	float4 clip = mul(g_View.CascadeViewProj[cascade], float4(position, 1.0));
	float3 ndc = clip.xyz / clip.w;
	float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
		return 1.0;
	return g_CascadeShadows.SampleCmpLevelZero(g_ShadowSampler, float3(uv, cascade), ndc.z - 0.001);
}

float FogSpotShadow(GPULight light, float3 position)
{
	float4 clip = mul(light.ShadowViewProj, float4(position, 1.0));
	float3 ndc = clip.xyz / clip.w;
	float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0 || clip.w <= 0.0)
		return 1.0;
	return g_SpotShadows.SampleCmpLevelZero(g_ShadowSampler, float3(uv, light.ShadowIndex), ndc.z - 0.0005);
}

float FogPointShadow(GPULight light, float3 position)
{
	float3 toPoint = position - light.Position;
	float3 absolute = abs(toPoint);
	float majorAxis = max(absolute.x, max(absolute.y, absolute.z));
	float n = light.ShadowNear, f = light.ShadowFar;
	float depth = f / (f - n) - (f * n) / ((f - n) * majorAxis) - 0.001;
	return g_PointShadows.SampleCmpLevelZero(g_ShadowSampler, float4(toPoint, light.ShadowIndex), depth);
}

[numthreads(4, 4, 4)]
void CSFogScatter(uint3 id : SV_DispatchThreadID)
{
	uint width, height, depth;
	g_FogScatter.GetDimensions(width, height, depth);
	if (any(id >= uint3(width, height, depth)))
		return;

	// The froxel's sample: its pixel column, at this frame's jittered depth inside its slice.
	float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
	float viewDepth = FogSliceDepth((id.z + g_View.FogJitter) / depth);
	float4 farClip = mul(g_View.InvViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0));
	float3 direction = normalize(farClip.xyz / farClip.w - g_View.CameraPosition);
	float4 centreClip = mul(g_View.InvViewProj, float4(0.0, 0.0, 1.0, 1.0));
	float3 forward = normalize(centreClip.xyz / centreClip.w - g_View.CameraPosition);
	float3 position = g_View.CameraPosition + direction * (viewDepth / max(dot(direction, forward), 1e-3));

	float density = FogDensityAt(position);
	float4 result = 0.0;
	if (density > 0.0)
	{
		float3 toEye = -direction;
		float3 light = 0.0;
		for (uint i = 0; i < g_View.LightCount; i++)
		{
			GPULight lamp = g_Lights[i];
			float3 L;
			float attenuation = 1.0;
			float shadow = 1.0;
			if (lamp.Type == LightDirectional)
			{
				L = -lamp.Direction;
				if (lamp.ShadowIndex >= 0)
					shadow = FogDirectionalShadow(position, viewDepth);
			}
			else
			{
				float3 toLight = lamp.Position - position;
				float distance = length(toLight);
				if (distance >= lamp.Range)
					continue;
				L = toLight / max(distance, 1e-4);
				attenuation = DistanceAttenuation(distance, lamp.Range);
				if (lamp.Type == LightSpot)
					attenuation *= SpotAttenuation(-L, lamp.Direction, lamp.CosOuter, lamp.CosInner);
				if (attenuation <= 0.0)
					continue;
				if (lamp.ShadowIndex >= 0)
					shadow = lamp.Type == LightSpot ? FogSpotShadow(lamp, position) : FogPointShadow(lamp, position);
				else
					shadow = g_FogPush.Params0.y; // no shadow map: light would show through walls, so less of it
			}
			light += lamp.Color * attenuation * shadow * FogPhase(dot(-L, toEye), g_View.FogAnisotropy);
		}
		result = float4(density * (g_View.FogColor + g_View.FogScattering * light), density);
	}

	// Blend with last frame's value for the same point, where it was in view.
	float historyWeight = g_FogPush.Params0.x;
	if (historyWeight > 0.0)
	{
		float4 previous = mul(g_View.PrevViewProj, float4(position, 1.0));
		if (previous.w > 0.0)
		{
			float2 previousUV = float2(previous.x / previous.w * 0.5 + 0.5, 0.5 - previous.y / previous.w * 0.5);
			float previousSlice = FogSliceCoordinate(previous.w);
			if (all(previousUV >= 0.0) && all(previousUV <= 1.0) && previous.w < g_View.FogDistance)
				result = lerp(result, g_FogHistory.SampleLevel(g_LinearClamp, float3(previousUV, previousSlice), 0), historyWeight);
		}
	}
	g_FogScatter[id] = result;
}
