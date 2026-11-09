#ifndef PENUMBRA_VIEW_BINDINGS_HLSLI
#define PENUMBRA_VIEW_BINDINGS_HLSLI

#include "Common.hlsli"

// Space 0: per-view resources (SceneRenderer::m_ViewLayout).
ConstantBuffer<ViewConstants> g_View : register(b0, space0);
StructuredBuffer<GPULight> g_Lights : register(t0, space0);
Texture2DArray<float> g_CascadeShadows : register(t1, space0);
Texture2DArray<float> g_SpotShadows : register(t2, space0);
TextureCubeArray<float> g_PointShadows : register(t3, space0);
TextureCube<float4> g_Irradiance : register(t4, space0);
TextureCube<float4> g_Prefiltered : register(t5, space0);
Texture2D<float2> g_BRDFLut : register(t6, space0);
Texture2D<float> g_AmbientOcclusion : register(t7, space0);
SamplerState g_LinearWrap : register(s0, space0);
SamplerComparisonState g_ShadowSampler : register(s1, space0);
SamplerState g_LinearClamp : register(s2, space0);
SamplerState g_PointClamp : register(s3, space0);

#endif
