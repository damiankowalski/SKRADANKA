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
// Flames: procedural fire on camera-facing billboards, additive over the HDR image.
// Params0: xyz base position, w height. Params1: x radius, y intensity, z time, w seed.
// Params2: rgb body colour. One instance per layer; layers differ in seed, size and offset.
// =============================================================================================

static const uint FlameLayers = 3;

struct FlameOutput
{
	float4 Position : SV_Position;
	float2 Local : TEXCOORD0; // x in [-1, 1] across, y in [0, 1] up
	float Layer : TEXCOORD1;
	float Depth : TEXCOORD2;
};

FlameOutput VSFlame(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	static const float2 Corners[6] = { float2(-1, 0), float2(1, 0), float2(1, 1), float2(-1, 0), float2(1, 1), float2(-1, 1) };
	float2 corner = Corners[vertexId];
	float layer = (float)instanceId;

	float3 base = g_Post.Params0.xyz;
	float height = g_Post.Params0.w * (1.0 - 0.15 * layer);
	float radius = g_Post.Params1.x * (1.0 + 0.35 * layer);

	// Cylindrical billboard: stays upright, turns around Y towards the camera.
	float3 toCamera = g_View.CameraPosition - base;
	toCamera.y = 0.0;
	toCamera = length(toCamera) > 1e-4 ? normalize(toCamera) : float3(0, 0, 1);
	float3 right = normalize(cross(float3(0, 1, 0), toCamera));
	float sway = (layer - 1.0) * 0.18 * radius;

	// Widen the quad: the flame shape is narrower than the billboard.
	float3 world = base + right * (corner.x * radius * 1.6 + sway) + float3(0, corner.y * height, 0) + toCamera * (0.02 * layer);
	FlameOutput output;
	output.Position = mul(g_View.ViewProj, float4(world, 1.0));
	output.Local = float2(corner.x * 1.6, corner.y);
	output.Layer = layer;
	output.Depth = output.Position.w;
	return output;
}

float Hash21(float2 p)
{
	p = frac(p * float2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	float2 u = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(Hash21(i), Hash21(i + float2(1, 0)), u.x), lerp(Hash21(i + float2(0, 1)), Hash21(i + float2(1, 1)), u.x), u.y);
}

float FractalNoise(float2 p)
{
	float value = 0.0, amplitude = 0.5;
	[unroll] for (int octave = 0; octave < 4; octave++)
	{
		value += ValueNoise(p) * amplitude;
		p = p * 2.03 + 17.1;
		amplitude *= 0.5;
	}
	return value;
}

float4 PSFlame(FlameOutput input) : SV_Target0
{
	float time = g_Post.Params1.z;
	float seed = g_Post.Params1.w + input.Layer * 7.31;
	float2 uv = input.Local;

	// Turbulence rising through the flame; tongues lick upwards and sway sideways.
	float rise = FractalNoise(float2(uv.x * 2.6 + seed, uv.y * 3.0 - time * 2.6));
	float swirl = FractalNoise(float2(uv.y * 1.3 - time * 0.9, seed * 0.37)) - 0.5;
	float x = uv.x + swirl * 0.55 * uv.y + (rise - 0.5) * 0.45 * uv.y;
	// Separate tongues higher up: the flame breaks into licks instead of one smooth cone.
	float tongues = ValueNoise(float2(x * 5.0 + seed, uv.y * 2.5 - time * 3.5));

	// Teardrop silhouette: wide at the base, a pointed tip that flickers in height.
	float tip = 0.75 + 0.25 * ValueNoise(float2(time * 3.0, seed));
	float width = pow(saturate(1.0 - uv.y / tip), 0.55) * smoothstep(0.0, 0.08, uv.y);
	float body = saturate(1.0 - abs(x) / max(width, 1e-3));
	float flame = saturate(body * (rise * 1.7 - uv.y * 0.6)) * lerp(1.0, saturate(tongues * 1.7), saturate(uv.y * 1.4));
	if (flame <= 0.002)
		discard;

	// Colour ramp: deep red edges -> body colour -> white-yellow core.
	float3 deep = float3(0.55, 0.05, 0.01);
	float3 core = float3(1.0, 0.78, 0.4);
	float3 color = lerp(deep, g_Post.Params2.rgb, smoothstep(0.08, 0.5, flame));
	color = lerp(color, core, smoothstep(0.7, 1.0, flame) * saturate(1.0 - uv.y * 1.2));
	float intensity = g_Post.Params1.y / (1.0 + input.Layer);
	return float4(color * flame * intensity, 1.0);
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
