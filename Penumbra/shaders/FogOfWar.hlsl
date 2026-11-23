#include "PostCommon.hlsli"
#include "FogOfWar.hlsli"

// =============================================================================================
// Fog of war mask: 1 where the player's eye sees the pixel's surface, 0 where it does not.
// The eye depth cube is rendered from the eye like a point light's shadow map (90 degree faces,
// mirrored X, D3D face order) from walls, floors and furniture only.
//   Params0: xyz eye, w half angle (degrees)
//   Params1: xy forward (xz plane), z edge softness (degrees), w view distance
//   Params2: x awareness radius, y cube near plane, z cube far plane
//   Params3: xyz where the cone starts (the camera, or the eye), w see-over height (metres)
// =============================================================================================

Texture2D<float> g_SceneDepth : register(t0);
TextureCube<float> g_EyeDepth : register(t1);

// Distance along the cube's major axis at which the eye sees the first surface in this direction.
float EyeDistance(float3 direction, float n, float f)
{
	float stored = g_EyeDepth.SampleLevel(g_PointClamp, direction, 0);
	return f * n / max(f - stored * (f - n), 1e-6);
}

// 0..1: how much of the point the eye sees past walls (a few taps around the direction soften the edges).
float SeenFromEye(float3 eye, float3 p, float n, float f)
{
	float3 toPoint = p - eye;
	float3 absolute = abs(toPoint);
	float majorAxis = max(absolute.x, max(absolute.y, absolute.z));
	float3 direction = normalize(toPoint);
	float3 tangent = normalize(cross(direction, abs(direction.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0)));
	float3 bitangent = cross(direction, tangent);
	float spread = 0.004;
	float bias = 0.06 + majorAxis * 0.015;
	float visible = 0.0;
	static const float2 Taps[5] = { float2(0, 0), float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1) };
	[unroll] for (uint i = 0; i < 5; i++)
	{
		float3 sampleDirection = direction + (tangent * Taps[i].x + bitangent * Taps[i].y) * spread;
		visible += majorAxis <= EyeDistance(sampleDirection, n, f) + bias ? 1.0 : 0.0;
	}
	return visible / 5.0;
}

float PSFogMask(FullscreenOutput input) : SV_Target0
{
	float depth = g_SceneDepth.SampleLevel(g_PointClamp, input.TexCoord, 0);
	float4 clip = float4(input.TexCoord.x * 2.0 - 1.0, 1.0 - input.TexCoord.y * 2.0, min(depth, 0.999999), 1.0);
	float4 world = mul(g_View.InvViewProj, clip);
	float3 p = world.xyz / world.w;

	float3 eye = g_Post.Params0.xyz;
	float3 coneOrigin = g_Post.Params3.xyz;
	float coverage = FogCoverage(eye, coneOrigin, g_Post.Params1.xy, g_Post.Params0.w, g_Post.Params1.z, g_Post.Params1.w, g_Post.Params2.x, p);
	if (depth >= 1.0)
	{
		// Sky: seen where the cone looks (nothing to test walls against); never beyond the view distance.
		float3 direction = normalize(p - g_View.CameraPosition);
		return FogCoverage(eye, coneOrigin, g_Post.Params1.xy, g_Post.Params0.w, g_Post.Params1.z, 1e9, 0.0, coneOrigin + direction * 10.0);
	}
	if (coverage <= 0.0)
		return 0.0;

	// Line of sight. The space above the surface, up to the eye, counts too (FogOfWarSystem::CanSee alike): the
	// floor behind a crate lower than the eye is seen, behind a wall taller than the eye it is not.
	float n = g_Post.Params2.y, f = g_Post.Params2.z;
	float visible = SeenFromEye(eye, p, n, f);
	float lifted = min(p.y + g_Post.Params3.w, eye.y);
	if (visible < 1.0 && lifted > p.y + 0.05)
		visible = max(visible, SeenFromEye(eye, float3(p.x, lifted, p.z), n, f));
	return coverage * visible;
}
