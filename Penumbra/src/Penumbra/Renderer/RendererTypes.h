#pragma once

#include <glm/glm.hpp>

#include <cstdint>

// C++ mirrors of the structures in Penumbra/shaders/Common.hlsli. Layouts are verified by the
// static_asserts below; change both sides together.

namespace Penumbra::GPU {

	constexpr uint32_t MaxCascades = 4;
	constexpr uint32_t MaxLights = 256;
	constexpr uint32_t MaxSpotShadows = 8;
	constexpr uint32_t MaxPointShadows = 4;

	constexpr uint32_t LightDirectional = 0;
	constexpr uint32_t LightPoint = 1;
	constexpr uint32_t LightSpot = 2;

	struct ViewConstants
	{
		glm::mat4 ViewProj;
		glm::mat4 View;
		glm::mat4 Proj;
		glm::mat4 InvProj;
		glm::mat4 InvViewProj;
		glm::mat4 CascadeViewProj[MaxCascades];
		glm::vec4 CascadeSplits;
		glm::vec3 CameraPosition;
		float Time;
		glm::vec2 ViewportSize;
		glm::vec2 InvViewportSize;
		glm::vec3 AmbientColor;
		float EnvironmentIntensity;
		glm::vec3 FogColor;
		float FogDensity;
		uint32_t LightCount;
		uint32_t HasEnvironment;
		float SkyboxIntensity;
		float Exposure;
		float ShadowSoftness;
		float CascadeCount;
		float PrefilteredMipCount;
		float SSAOEnabled;
		float SSAORadius;
		float SSAOIntensity;
		float BloomIntensity;
		float DirectionalShadowIndex;
	};
	static_assert(sizeof(ViewConstants) == 704, "ViewConstants must match Common.hlsli");

	struct Light
	{
		glm::vec3 Position;
		float Range;
		glm::vec3 Color;
		uint32_t Type;
		glm::vec3 Direction;
		float CosOuter;
		float CosInner;
		int32_t ShadowIndex;
		float ShadowNear;
		float ShadowFar;
		glm::mat4 ShadowViewProj;
	};
	static_assert(sizeof(Light) == 128, "GPULight must match Common.hlsli");

	struct DrawConstants
	{
		glm::vec4 WorldRow0;
		glm::vec4 WorldRow1;
		glm::vec4 WorldRow2;
		glm::vec4 BaseColor;
		glm::vec3 Emissive;
		float Metallic;
		float Roughness;
		uint32_t TextureFlags;
		glm::vec2 Padding;
	};
	static_assert(sizeof(DrawConstants) == 96, "DrawConstants must match Common.hlsli");

	struct ShadowConstants
	{
		glm::mat4 ViewProj;
		glm::vec4 WorldRow0;
		glm::vec4 WorldRow1;
		glm::vec4 WorldRow2;
	};
	static_assert(sizeof(ShadowConstants) == 112, "ShadowConstants must match Shadow.hlsl");

	struct SkinningConstants
	{
		uint32_t VertexCount;
		uint32_t JointCount;
		uint32_t Padding[2];
	};
	static_assert(sizeof(SkinningConstants) == 16, "SkinningConstants must match Skinning.hlsl");

	struct PostConstants
	{
		glm::vec4 Params0{ 0.0f };
		glm::vec4 Params1{ 0.0f };
		glm::vec4 Params2{ 0.0f };
	};
	static_assert(sizeof(PostConstants) == 48, "PostConstants must match PostCommon.hlsli");

	struct IBLConstants
	{
		uint32_t FaceSize;
		float Roughness;
		float SourceMip;
		uint32_t SampleCount;
	};
	static_assert(sizeof(IBLConstants) == 16, "IBLConstants must match IBL.hlsl");

	struct OverlayVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
	};

	enum TextureFlag : uint32_t
	{
		HasAlbedo = 1u << 0,
		HasNormal = 1u << 1,
		HasMetallicRoughness = 1u << 2,
		HasEmissive = 1u << 3
	};

	// Rows of the affine part of a glm (column-major) matrix, as consumed by WorldMatrix() in HLSL.
	inline void PackWorldRows(const glm::mat4& m, glm::vec4& row0, glm::vec4& row1, glm::vec4& row2)
	{
		row0 = glm::vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
		row1 = glm::vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
		row2 = glm::vec4(m[0][2], m[1][2], m[2][2], m[3][2]);
	}

}
