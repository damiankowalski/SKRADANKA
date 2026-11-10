#ifndef PENUMBRA_LIGHTING_HLSLI
#define PENUMBRA_LIGHTING_HLSLI

#include "ViewBindings.hlsli"

// Mirrors Penumbra/src/Penumbra/Scene/LightUtils.h (perception uses the same falloff).
float DistanceAttenuation(float distance, float range)
{
	if (range <= 0.0 || distance >= range)
		return 0.0;
	float ratio = distance / range;
	float ratio4 = ratio * ratio * ratio * ratio;
	float window = saturate(1.0 - ratio4);
	return (window * window) / (distance * distance + 1.0);
}

float SpotAttenuation(float3 lightToPoint, float3 spotDirection, float cosOuter, float cosInner)
{
	float cosAngle = dot(normalize(lightToPoint), normalize(spotDirection));
	float t = saturate((cosAngle - cosOuter) / max(cosInner - cosOuter, 1e-4));
	return t * t * (3.0 - 2.0 * t);
}

// ---------------------------------------------------------------------------------------------
// Cook-Torrance GGX BRDF
// ---------------------------------------------------------------------------------------------

float DistributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float denominator = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * denominator * denominator, 1e-7);
}

float GeometrySmithGGX(float NdotV, float NdotL, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	float gv = NdotV / (NdotV * (1.0 - k) + k);
	float gl = NdotL / (NdotL * (1.0 - k) + k);
	return gv * gl;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	return F0 + (max(1.0 - roughness, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 albedo, float metallic, float roughness)
{
	float3 H = normalize(V + L);
	float NdotL = saturate(dot(N, L));
	float NdotV = max(dot(N, V), 1e-4);
	float NdotH = saturate(dot(N, H));
	float VdotH = saturate(dot(V, H));

	float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	float3 F = FresnelSchlick(VdotH, F0);
	float D = DistributionGGX(NdotH, roughness);
	float G = GeometrySmithGGX(NdotV, NdotL, roughness);
	float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
	float3 kd = (1.0 - F) * (1.0 - metallic);
	return (kd * albedo / PI + specular) * NdotL;
}

// ---------------------------------------------------------------------------------------------
// Soft shadows: PCSS-style blocker search followed by Poisson-disc PCF.
// ---------------------------------------------------------------------------------------------

static const float2 PoissonDisk[16] = {
	float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
	float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
	float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
	float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100029)
};

float2 RotateDisk(float2 offset, float angle)
{
	float s, c;
	sincos(angle, s, c);
	return float2(c * offset.x - s * offset.y, s * offset.x + c * offset.y);
}

// uv/depth in shadow-map space of one array slice. lightSize is in UV units.
float SampleShadowArray(Texture2DArray<float> map, float3 uv, float depth, float lightSize, float noise)
{
	float angle = noise * 2.0 * PI;

	// Blocker search: average depth of occluders in the search region.
	float blockerSum = 0.0;
	float blockers = 0.0;
	[unroll] for (uint i = 0; i < 16; i++)
	{
		float2 offset = RotateDisk(PoissonDisk[i], angle) * lightSize;
		float sampleDepth = map.SampleLevel(g_PointClamp, float3(uv.xy + offset, uv.z), 0);
		if (sampleDepth < depth)
		{
			blockerSum += sampleDepth;
			blockers += 1.0;
		}
	}
	if (blockers == 0.0)
		return 1.0;

	// Penumbra width grows with the receiver-blocker distance.
	float averageBlocker = blockerSum / blockers;
	float penumbra = clamp((depth - averageBlocker) / max(averageBlocker, 1e-4), 0.05, 1.0) * lightSize;

	float lit = 0.0;
	[unroll] for (uint j = 0; j < 16; j++)
	{
		float2 offset = RotateDisk(PoissonDisk[j], angle) * penumbra;
		lit += map.SampleCmpLevelZero(g_ShadowSampler, float3(uv.xy + offset, uv.z), depth);
	}
	return lit / 16.0;
}

float DirectionalShadow(float3 worldPosition, float3 normal, float viewDepth, float noise)
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

	// Normal offset scales with the cascade's texel size to fight acne without peter-panning.
	float3 offsetPosition = worldPosition + normal * (0.02 * (cascade + 1));
	float4 clip = mul(g_View.CascadeViewProj[cascade], float4(offsetPosition, 1.0));
	float3 ndc = clip.xyz / clip.w;
	float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
		return 1.0;
	float lightSize = 0.0015 * g_View.ShadowSoftness / (cascade + 1);
	return SampleShadowArray(g_CascadeShadows, float3(uv, cascade), ndc.z - 0.0005, lightSize, noise);
}

float SpotShadow(GPULight light, float3 worldPosition, float3 normal, float noise)
{
	float4 clip = mul(light.ShadowViewProj, float4(worldPosition + normal * 0.02, 1.0));
	float3 ndc = clip.xyz / clip.w;
	float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0 || clip.w <= 0.0)
		return 1.0;
	return SampleShadowArray(g_SpotShadows, float3(uv, light.ShadowIndex), ndc.z - 0.0002, 0.004 * g_View.ShadowSoftness, noise);
}

float PointShadow(GPULight light, float3 worldPosition, float3 normal, float noise)
{
	float3 toPoint = worldPosition + normal * 0.03 - light.Position;
	float3 absolute = abs(toPoint);
	float majorAxis = max(absolute.x, max(absolute.y, absolute.z));
	float n = light.ShadowNear, f = light.ShadowFar;
	// Hardware depth the point would have in the face it projects to (perspective, 90 degrees).
	float depth = f / (f - n) - (f * n) / ((f - n) * majorAxis) - 0.0005;

	float3 direction = normalize(toPoint);
	float3 tangent = normalize(cross(direction, abs(direction.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0)));
	float3 bitangent = cross(direction, tangent);
	float radius = 0.02 * g_View.ShadowSoftness;
	float angle = noise * 2.0 * PI;
	float lit = 0.0;
	[unroll] for (uint i = 0; i < 16; i++)
	{
		float2 offset = RotateDisk(PoissonDisk[i], angle) * radius;
		float3 sampleDirection = direction + tangent * offset.x + bitangent * offset.y;
		lit += g_PointShadows.SampleCmpLevelZero(g_ShadowSampler, float4(sampleDirection, light.ShadowIndex), depth);
	}
	return lit / 16.0;
}

// ---------------------------------------------------------------------------------------------
// Direct + image-based lighting
// ---------------------------------------------------------------------------------------------

float3 EvaluateLights(float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float viewDepth, float noise)
{
	float3 result = 0.0;
	for (uint i = 0; i < g_View.LightCount; i++)
	{
		GPULight light = g_Lights[i];
		float3 L;
		float attenuation = 1.0;
		float shadow = 1.0;

		if (light.Type == LightDirectional)
		{
			L = -light.Direction;
			if (light.ShadowIndex >= 0)
				shadow = DirectionalShadow(P, N, viewDepth, noise);
		}
		else
		{
			float3 toLight = light.Position - P;
			float distance = length(toLight);
			if (distance >= light.Range)
				continue;
			L = toLight / max(distance, 1e-4);
			attenuation = DistanceAttenuation(distance, light.Range);
			if (light.Type == LightSpot)
			{
				attenuation *= SpotAttenuation(-L, light.Direction, light.CosOuter, light.CosInner);
				if (attenuation > 0.0 && light.ShadowIndex >= 0)
					shadow = SpotShadow(light, P, N, noise);
			}
			else if (light.ShadowIndex >= 0)
			{
				shadow = PointShadow(light, P, N, noise);
			}
		}

		if (attenuation <= 0.0 || shadow <= 0.0)
			continue;
		result += EvaluateBRDF(N, V, L, albedo, metallic, roughness) * light.Color * attenuation * shadow;
	}
	return result;
}

float3 EvaluateAmbient(float3 N, float3 V, float3 albedo, float metallic, float roughness, float ao)
{
	float NdotV = max(dot(N, V), 1e-4);
	float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	float3 kd = (1.0 - F) * (1.0 - metallic);

	float3 irradiance;
	float3 prefiltered;
	if (g_View.HasEnvironment != 0)
	{
		irradiance = g_Irradiance.SampleLevel(g_LinearClamp, N, 0).rgb * g_View.EnvironmentIntensity;
		float3 R = reflect(-V, N);
		prefiltered = g_Prefiltered.SampleLevel(g_LinearClamp, R, roughness * (g_View.PrefilteredMipCount - 1.0)).rgb * g_View.EnvironmentIntensity;
	}
	else
	{
		irradiance = g_View.AmbientColor;
		prefiltered = g_View.AmbientColor;
	}

	float2 brdf = g_BRDFLut.SampleLevel(g_LinearClamp, float2(NdotV, roughness), 0);
	float3 diffuse = kd * albedo * irradiance;
	float3 specular = prefiltered * (F * brdf.x + brdf.y);
	// Specular occlusion approximation from ambient occlusion.
	float specularOcclusion = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
	return diffuse * ao + specular * specularOcclusion;
}

float3 ApplyFog(float3 color, float distance)
{
	float fog = 1.0 - exp(-g_View.FogDensity * distance);
	return lerp(color, g_View.FogColor, saturate(fog));
}

#endif
