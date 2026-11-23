#ifndef PENUMBRA_FOG_OF_WAR_HLSLI
#define PENUMBRA_FOG_OF_WAR_HLSLI

// Fog of war: what the player's eye covers. Mirrors FogOfWarView::Coverage in
// Penumbra/src/Penumbra/AI/FogOfWarSystem.cpp: change both or neither (the CPU decides which
// characters are drawn, the GPU which pixels are darkened; they must agree).
//   eye, coneOrigin (the camera, or the eye), forward (horizontal, normalised xz), halfAngle and edgeSoftness in
//   degrees.
float FogCoverage(float3 eye, float3 coneOrigin, float2 forward, float halfAngle, float edgeSoftness, float viewDistance, float awarenessRadius,
	float3 p)
{
	float3 offset = p - eye;
	float eyeDistance = length(offset.xz);
	float2 flat = (p - coneOrigin).xz;
	float flatDistance = length(flat);

	float circle = 1.0 - smoothstep(awarenessRadius * 0.7, max(awarenessRadius, 1e-4), eyeDistance);
	float cone = 1.0;
	if (flatDistance > 1e-4)
	{
		float cosAngle = clamp(dot(flat / flatDistance, forward), -1.0, 1.0);
		float angle = degrees(acos(cosAngle));
		cone = 1.0 - smoothstep(halfAngle - edgeSoftness * 0.5, halfAngle + edgeSoftness * 0.5 + 1e-4, angle);
	}
	float range = 1.0 - smoothstep(viewDistance * 0.85, max(viewDistance, 1e-4), length(offset));
	return max(circle, cone) * range;
}

#endif
