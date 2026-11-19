#include "Lighting.hlsli"

// Space 1: per-draw material data (SceneRenderer::m_MaterialLayout).
PN_PUSH_CONSTANT ConstantBuffer<DrawConstants> g_Draw : register(b0, space1);
Texture2D<float4> g_AlbedoTexture : register(t0, space1);
Texture2D<float4> g_NormalTexture : register(t1, space1);
Texture2D<float4> g_MetallicRoughnessTexture : register(t2, space1);
Texture2D<float4> g_EmissiveTexture : register(t3, space1);

struct VertexInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Normal : NORMAL;
	float4 Tangent : TANGENT;
	float2 TexCoord : TEXCOORD0;
};

// The prepass and the lighting pass share this vertex shader so depths match exactly.
VertexOutput VSMain(VertexInput input)
{
	float4x4 world = WorldMatrix(g_Draw);
	float3x3 normalMatrix = NormalMatrix(world);

	VertexOutput output;
	float4 worldPosition = mul(world, float4(input.Position, 1.0));
	output.Position = mul(g_View.ViewProj, worldPosition);
	output.WorldPosition = worldPosition.xyz;
	output.Normal = mul(normalMatrix, input.Normal);
	output.Tangent = float4(mul((float3x3)world, input.Tangent.xyz), input.Tangent.w);
	output.TexCoord = input.TexCoord;
	return output;
}

struct SurfaceData
{
	float3 Albedo;
	float Alpha;
	float3 Normal;
	float Metallic;
	float Roughness;
	float3 Emissive;
};

SurfaceData GetSurface(VertexOutput input, bool frontFace)
{
	SurfaceData surface;
	// The material's tiling (MaterialComponent::UVScale, UVOffset) applies to every texture alike.
	float2 uv = input.TexCoord * g_Draw.UVScale + g_Draw.UVOffset;
	float4 baseColor = g_Draw.BaseColor;
	if (g_Draw.TextureFlags & 1)
		baseColor *= g_AlbedoTexture.Sample(g_LinearWrap, uv);
	surface.Albedo = baseColor.rgb;
	surface.Alpha = baseColor.a;

	float3 N = normalize(input.Normal);
	if (!frontFace)
		N = -N;
	if (g_Draw.TextureFlags & 2)
	{
		float3 T = normalize(input.Tangent.xyz - N * dot(input.Tangent.xyz, N));
		float3 B = cross(N, T) * input.Tangent.w;
		float3 tangentNormal = g_NormalTexture.Sample(g_LinearWrap, uv).xyz * 2.0 - 1.0;
		N = normalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * N);
	}
	surface.Normal = N;

	surface.Metallic = g_Draw.Metallic;
	surface.Roughness = g_Draw.Roughness;
	if (g_Draw.TextureFlags & 4)
	{
		float4 mr = g_MetallicRoughnessTexture.Sample(g_LinearWrap, uv);
		surface.Roughness *= mr.g;
		surface.Metallic *= mr.b;
	}
	surface.Roughness = clamp(surface.Roughness, 0.04, 1.0);

	surface.Emissive = g_Draw.Emissive;
	if (g_Draw.TextureFlags & 8)
		surface.Emissive *= g_EmissiveTexture.Sample(g_LinearWrap, uv).rgb;
	return surface;
}

// Depth + view-space normal prepass (feeds SSAO).
float4 PSPrepass(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	float3 N = normalize(input.Normal);
	if (!frontFace)
		N = -N;
	float3 viewNormal = normalize(mul((float3x3)g_View.View, N));
	return float4(viewNormal * 0.5 + 0.5, 1.0);
}

float4 PSForward(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	SurfaceData surface = GetSurface(input, frontFace);
	float3 V = g_View.CameraPosition - input.WorldPosition;
	float distance = length(V);
	V /= max(distance, 1e-4);
	float viewDepth = -mul(g_View.View, float4(input.WorldPosition, 1.0)).z;
	float noise = InterleavedGradientNoise(input.Position.xy);

	float ao = 1.0;
	if (g_View.SSAOEnabled > 0.5)
		ao = g_AmbientOcclusion.SampleLevel(g_PointClamp, input.Position.xy * g_View.InvViewportSize, 0);

	float3 color = EvaluateLights(input.WorldPosition, surface.Normal, V, surface.Albedo, surface.Metallic, surface.Roughness, viewDepth, noise);
	color += EvaluateAmbient(surface.Normal, V, surface.Albedo, surface.Metallic, surface.Roughness, ao);
	color += surface.Emissive;
	color = ApplyFog(color, input.WorldPosition, input.Position.xy * g_View.InvViewportSize, viewDepth, g_FogVolume, g_LinearClamp);
	return float4(color, surface.Alpha);
}
