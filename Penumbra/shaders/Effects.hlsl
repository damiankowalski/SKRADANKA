#include "PostCommon.hlsli"

// =============================================================================================
// Smoke volumes: analytic ray/sphere extinction, the same model the perception system uses
// (transmittance = exp(-density * path length)), so what blocks a guard's view is what the
// player sees. Drawn as a full-screen pass per volume over the HDR target.
// Params0: xyz centre, w radius. Params1: x density, y fade (0..1).
// =============================================================================================

Texture2D<float> g_SceneDepth : register(t0);

float4 PSSmoke(FullscreenOutput input) : SV_Target0
{
	float4 clip = float4(input.TexCoord.x * 2.0 - 1.0, 1.0 - input.TexCoord.y * 2.0, 1.0, 1.0);
	float4 farPoint = mul(g_View.InvViewProj, clip);
	float3 direction = normalize(farPoint.xyz / farPoint.w - g_View.CameraPosition);

	float depth = g_SceneDepth.SampleLevel(g_PointClamp, input.TexCoord, 0);
	float4 sceneClip = float4(clip.xy, depth, 1.0);
	float4 scenePoint = mul(g_View.InvViewProj, sceneClip);
	float sceneDistance = depth >= 1.0 ? 1e6 : length(scenePoint.xyz / scenePoint.w - g_View.CameraPosition);

	float3 center = g_Post.Params0.xyz;
	float radius = g_Post.Params0.w;
	float3 toOrigin = g_View.CameraPosition - center;
	float b = dot(toOrigin, direction);
	float c = dot(toOrigin, toOrigin) - radius * radius;
	float discriminant = b * b - c;
	if (discriminant <= 0.0)
		discard;
	float root = sqrt(discriminant);
	float enter = max(-b - root, 0.0);
	float exit = min(-b + root, sceneDistance);
	float length = max(exit - enter, 0.0);
	if (length <= 0.0)
		discard;

	// Softer edges: weight the chord by how close it passes to the centre.
	float closest = sqrt(max(dot(toOrigin, toOrigin) - b * b, 0.0));
	float edge = saturate(1.0 - closest / radius);
	float opacity = (1.0 - exp(-g_Post.Params1.x * length)) * smoothstep(0.0, 0.35, edge) * g_Post.Params1.y;
	float3 color = g_View.AmbientColor * 2.0 + g_View.FogColor + 0.02;
	return float4(color, opacity);
}

// =============================================================================================
// Overlay: HUD rectangles/text (pixel space) and debug lines (world space).
// =============================================================================================

struct OverlayVertex
{
	float3 Position : POSITION;
	float4 Color : COLOR0;
};

struct OverlayOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR0;
};

OverlayOutput VSOverlay2D(OverlayVertex input)
{
	OverlayOutput output;
	float2 ndc = input.Position.xy * g_View.InvViewportSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
	output.Position = float4(ndc, 0.0, 1.0);
	output.Color = input.Color;
	return output;
}

OverlayOutput VSOverlay3D(OverlayVertex input)
{
	OverlayOutput output;
	output.Position = mul(g_View.ViewProj, float4(input.Position, 1.0));
	output.Color = input.Color;
	return output;
}

float4 PSOverlay(OverlayOutput input) : SV_Target0
{
	return input.Color;
}
