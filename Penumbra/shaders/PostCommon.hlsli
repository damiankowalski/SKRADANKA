#ifndef PENUMBRA_POST_COMMON_HLSLI
#define PENUMBRA_POST_COMMON_HLSLI

#include "Common.hlsli"

// Shared binding layout of every full-screen pass (SceneRenderer::m_PostLayout):
//   b0 view constants, b1 push constants, t0..t2 inputs, s0 linear clamp, s1 point clamp.
struct PostConstants
{
	float4 Params0;
	float4 Params1;
	float4 Params2;
};

ConstantBuffer<ViewConstants> g_View : register(b0);
ConstantBuffer<PostConstants> g_Post : register(b1);
SamplerState g_LinearClamp : register(s0);
SamplerState g_PointClamp : register(s1);

struct FullscreenOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

// One triangle covering the screen, no vertex buffer.
FullscreenOutput VSFullscreen(uint vertexId : SV_VertexID)
{
	FullscreenOutput output;
	output.TexCoord = float2((vertexId << 1) & 2, vertexId & 2);
	output.Position = float4(output.TexCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return output;
}

#endif
