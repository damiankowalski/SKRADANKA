#include "PostCommon.hlsli"
#include "Fog.hlsli"

// =============================================================================================
// Sky: environment cube map (or ambient colour) behind all geometry. Drawn at depth 1.
// =============================================================================================

TextureCube<float4> g_SkyCube : register(t0);
Texture3D<float4> g_SkyFog : register(t1); // the integrated volumetric fog (Fog.hlsli)

FullscreenOutput VSSky(uint vertexId : SV_VertexID)
{
	FullscreenOutput output = VSFullscreen(vertexId);
	output.Position.z = 1.0;
	return output;
}

float4 PSSky(FullscreenOutput input) : SV_Target0
{
	float4 clip = float4(input.TexCoord.x * 2.0 - 1.0, 1.0 - input.TexCoord.y * 2.0, 1.0, 1.0);
	float4 world = mul(g_View.InvViewProj, clip);
	float3 direction = normalize(world.xyz / world.w - g_View.CameraPosition);
	float3 color = g_View.HasEnvironment != 0 ? g_SkyCube.SampleLevel(g_LinearClamp, direction, 0).rgb : g_View.AmbientColor;
	color *= g_View.SkyboxIntensity;
	// The fog veils the sky as it would a wall 200 m away.
	float3 farPoint = g_View.CameraPosition + direction * 200.0;
	float viewDepth = -mul(g_View.View, float4(farPoint, 1.0)).z;
	return float4(ApplyFog(color, farPoint, input.TexCoord, viewDepth, g_SkyFog, g_LinearClamp), 1.0);
}

// =============================================================================================
// SSAO: normal-oriented hemisphere sampling on the prepass depth/normals.
// Params0: x radius (m), y intensity, z bias
// =============================================================================================

Texture2D<float> g_Depth : register(t0);
Texture2D<float4> g_Normals : register(t1);

static const uint SSAOSamples = 16;

float3 HemisphereSample(uint index, float rotation)
{
	// Fibonacci spiral over the hemisphere, scaled so more samples sit near the centre.
	float i = (float)index + 0.5;
	float cosTheta = 1.0 - i / SSAOSamples;
	float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
	float phi = i * 2.39996323 + rotation * 2.0 * PI;
	float scale = lerp(0.1, 1.0, (i / SSAOSamples) * (i / SSAOSamples));
	return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta) * scale;
}

float PSSSAO(FullscreenOutput input) : SV_Target0
{
	float depth = g_Depth.SampleLevel(g_PointClamp, input.TexCoord, 0);
	if (depth >= 1.0)
		return 1.0;

	float3 position = ReconstructViewPosition(input.TexCoord, depth, g_View.InvProj);
	float3 normal = normalize(g_Normals.SampleLevel(g_PointClamp, input.TexCoord, 0).xyz * 2.0 - 1.0);
	float radius = g_Post.Params0.x;
	float bias = g_Post.Params0.z;

	float3 tangent = normalize(cross(normal, abs(normal.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0)));
	float3 bitangent = cross(normal, tangent);
	float rotation = InterleavedGradientNoise(input.Position.xy);

	float occlusion = 0.0;
	[unroll] for (uint i = 0; i < SSAOSamples; i++)
	{
		float3 h = HemisphereSample(i, rotation);
		float3 samplePosition = position + (tangent * h.x + bitangent * h.y + normal * h.z) * radius;
		float4 clip = mul(g_View.Proj, float4(samplePosition, 1.0));
		float2 uv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
		if (any(uv < 0.0) || any(uv > 1.0))
			continue;
		float sceneDepth = g_Depth.SampleLevel(g_PointClamp, uv, 0);
		float3 scenePosition = ReconstructViewPosition(uv, sceneDepth, g_View.InvProj);
		// View space looks down -Z: a larger z is closer to the camera.
		float occluded = scenePosition.z >= samplePosition.z + bias ? 1.0 : 0.0;
		float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(position.z - scenePosition.z), 1e-4));
		occlusion += occluded * rangeCheck;
	}
	float ao = 1.0 - occlusion / SSAOSamples;
	return saturate(pow(ao, g_Post.Params0.y));
}

// Depth-aware separable blur. Params0.xy = texel step direction.
Texture2D<float> g_AOInput : register(t2);

float PSSSAOBlur(FullscreenOutput input) : SV_Target0
{
	float centerDepth = g_Depth.SampleLevel(g_PointClamp, input.TexCoord, 0);
	float3 centerPosition = ReconstructViewPosition(input.TexCoord, centerDepth, g_View.InvProj);
	float total = 0.0;
	float weights = 0.0;
	[unroll] for (int i = -4; i <= 4; i++)
	{
		float2 uv = input.TexCoord + g_Post.Params0.xy * i;
		float sampleDepth = g_Depth.SampleLevel(g_PointClamp, uv, 0);
		float3 samplePosition = ReconstructViewPosition(uv, sampleDepth, g_View.InvProj);
		float depthWeight = exp(-abs(samplePosition.z - centerPosition.z) * 4.0);
		float spatialWeight = exp(-(i * i) / 8.0);
		float weight = depthWeight * spatialWeight;
		total += g_AOInput.SampleLevel(g_PointClamp, uv, 0) * weight;
		weights += weight;
	}
	return total / max(weights, 1e-4);
}

// =============================================================================================
// Bloom: 13-tap downsample chain and tent upsample (Jimenez 2014).
// Params0.xy = source texel size, Params0.z = source mip
// =============================================================================================

Texture2D<float4> g_BloomSource : register(t0);

float4 PSBloomDownsample(FullscreenOutput input) : SV_Target0
{
	float2 t = g_Post.Params0.xy;
	float mip = g_Post.Params0.z;
	float2 uv = input.TexCoord;
	float3 a = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-2, -2), mip).rgb;
	float3 b = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(0, -2), mip).rgb;
	float3 c = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(2, -2), mip).rgb;
	float3 d = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-2, 0), mip).rgb;
	float3 e = g_BloomSource.SampleLevel(g_LinearClamp, uv, mip).rgb;
	float3 f = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(2, 0), mip).rgb;
	float3 g = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-2, 2), mip).rgb;
	float3 h = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(0, 2), mip).rgb;
	float3 i = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(2, 2), mip).rgb;
	float3 j = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-1, -1), mip).rgb;
	float3 k = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(1, -1), mip).rgb;
	float3 l = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-1, 1), mip).rgb;
	float3 m = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(1, 1), mip).rgb;
	float3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
	// Guard against NaN/Inf fireflies propagating through the chain.
	return float4(min(color, 65000.0), 1.0);
}

float4 PSBloomUpsample(FullscreenOutput input) : SV_Target0
{
	float2 t = g_Post.Params0.xy;
	float mip = g_Post.Params0.z;
	float2 uv = input.TexCoord;
	float3 color = g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-1, -1), mip).rgb;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(0, -1), mip).rgb * 2.0;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(1, -1), mip).rgb;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-1, 0), mip).rgb * 2.0;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv, mip).rgb * 4.0;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(1, 0), mip).rgb * 2.0;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(-1, 1), mip).rgb;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(0, 1), mip).rgb * 2.0;
	color += g_BloomSource.SampleLevel(g_LinearClamp, uv + t * float2(1, 1), mip).rgb;
	return float4(color / 16.0, 1.0);
}

// =============================================================================================
// Tonemapping: exposure, bloom composite, fog of war, ACES fitted curve, sRGB encoding.
// Params0: x fog of war on (1), y brightness and z saturation left where the player does not see.
// =============================================================================================

Texture2D<float4> g_HDR : register(t0);
Texture2D<float4> g_Bloom : register(t1);
Texture2D<float> g_FogMask : register(t2);

float3 ACESFitted(float3 color)
{
	// Stephen Hill's fit of the ACES RRT + ODT.
	static const float3x3 InputMatrix = { 0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777 };
	static const float3x3 OutputMatrix = { 1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602 };
	color = mul(InputMatrix, color);
	float3 a = color * (color + 0.0245786) - 0.000090537;
	float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	color = mul(OutputMatrix, a / b);
	return saturate(color);
}

float3 LinearToSRGB(float3 color)
{
	return select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
}

float4 PSTonemap(FullscreenOutput input) : SV_Target0
{
	float3 hdr = g_HDR.SampleLevel(g_PointClamp, input.TexCoord, 0).rgb;
	float3 bloom = g_Bloom.SampleLevel(g_LinearClamp, input.TexCoord, 0).rgb;
	float3 color = lerp(hdr, bloom, saturate(g_View.BloomIntensity)) * g_View.Exposure;
	if (g_Post.Params0.x > 0.5)
	{
		// What the player does not see is remembered: darker and drained of colour.
		float seen = g_FogMask.SampleLevel(g_LinearClamp, input.TexCoord, 0);
		float saturation = lerp(g_Post.Params0.z, 1.0, seen);
		color = lerp(Luminance(color).xxx, color, saturation) * lerp(g_Post.Params0.y, 1.0, seen);
	}
	color = ACESFitted(color);
	// Tiny dither breaks up banding in dark gradients (a stealth game lives in them).
	float dither = (InterleavedGradientNoise(input.Position.xy) - 0.5) / 255.0;
	return float4(LinearToSRGB(color) + dither, 1.0);
}

// =============================================================================================
// Blit: copies an LDR image (already tonemapped and sRGB encoded) to another target.
// =============================================================================================

float4 PSBlit(FullscreenOutput input) : SV_Target0
{
	return float4(g_HDR.SampleLevel(g_LinearClamp, input.TexCoord, 0).rgb, 1.0);
}
