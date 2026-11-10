#ifndef PENUMBRA_COMMON_HLSLI
#define PENUMBRA_COMMON_HLSLI

// Must match Penumbra/src/Penumbra/Renderer/RendererTypes.h exactly.

static const float PI = 3.14159265359;
static const uint MaxCascades = 4;

static const uint LightDirectional = 0;
static const uint LightPoint = 1;
static const uint LightSpot = 2;

struct ViewConstants
{
	float4x4 ViewProj;
	float4x4 View;
	float4x4 Proj;
	float4x4 InvProj;
	float4x4 InvViewProj;
	float4x4 CascadeViewProj[MaxCascades];
	float4 CascadeSplits;     // view-space distance where each cascade ends
	float3 CameraPosition;
	float Time;
	float2 ViewportSize;
	float2 InvViewportSize;
	float3 AmbientColor;
	float EnvironmentIntensity;
	float3 FogColor;
	float FogDensity;
	uint LightCount;
	uint HasEnvironment;
	float SkyboxIntensity;
	float Exposure;
	float ShadowSoftness;     // light size scale for PCSS
	float CascadeCount;
	float PrefilteredMipCount;
	float SSAOEnabled;
	float SSAORadius;
	float SSAOIntensity;
	float BloomIntensity;
	float DirectionalShadowIndex; // -1 when no directional shadow
};

struct GPULight
{
	float3 Position;
	float Range;
	float3 Color;            // colour * intensity
	uint Type;
	float3 Direction;
	float CosOuter;
	float CosInner;
	int ShadowIndex;         // slice (spot) or cube index (point), -1 = no shadow
	float ShadowNear;
	float ShadowFar;
	float4x4 ShadowViewProj; // spot lights
};

// Per-draw data (push constants): affine world matrix + material.
struct DrawConstants
{
	float4 WorldRow0;
	float4 WorldRow1;
	float4 WorldRow2;
	float4 BaseColor;
	float3 Emissive;
	float Metallic;
	float Roughness;
	uint TextureFlags;       // bit 0 albedo, 1 normal, 2 metallic-roughness, 3 emissive
	float2 Padding;
};

float4x4 WorldMatrix(DrawConstants draw)
{
	return float4x4(draw.WorldRow0, draw.WorldRow1, draw.WorldRow2, float4(0, 0, 0, 1));
}

// Cofactor matrix: transforms normals correctly under non-uniform scale (up to length).
float3x3 NormalMatrix(float4x4 world)
{
	float3 r0 = world[0].xyz;
	float3 r1 = world[1].xyz;
	float3 r2 = world[2].xyz;
	return float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1));
}

float3 ReconstructViewPosition(float2 uv, float depth, float4x4 invProj)
{
	float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	float4 view = mul(invProj, clip);
	return view.xyz / view.w;
}

float InterleavedGradientNoise(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float Luminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

#endif
