#pragma once

#include "Penumbra/Animation/AnimationData.h"

#include <glm/glm.hpp>

#include <limits>
#include <string>
#include <vector>

namespace Penumbra {

	struct Vertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f }; // xyz tangent, w handedness
		glm::vec2 TexCoord{ 0.0f };
	};

	enum class TextureFormat : uint8_t
	{
		RGBA8 = 0,  // 8-bit per channel, sRGB or linear depending on usage
		RGBA32F     // HDR environment maps
	};

	struct TextureData
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		TextureFormat Format = TextureFormat::RGBA8;
		std::vector<uint8_t> Pixels;

		bool IsValid() const { return Width > 0 && Height > 0 && !Pixels.empty(); }
		size_t GetBytesPerPixel() const { return Format == TextureFormat::RGBA8 ? 4 : 16; }
	};

	// Index into MeshData::Textures, or -1 for none.
	using TextureIndex = int32_t;

	struct MaterialData
	{
		std::string Name;
		glm::vec4 BaseColor{ 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.6f;
		glm::vec3 Emissive{ 0.0f };
		bool AlphaBlend = false;
		TextureIndex AlbedoTexture = -1;
		TextureIndex NormalTexture = -1;
		TextureIndex MetallicRoughnessTexture = -1;
		TextureIndex EmissiveTexture = -1;
	};

	struct Submesh
	{
		uint32_t IndexOffset = 0;
		uint32_t IndexCount = 0;
		uint32_t MaterialIndex = 0;
		std::string Node; // name of the source node, so parts can be hidden (MeshComponent::HiddenNodes)
	};

	struct BoundingBox
	{
		glm::vec3 Min{ std::numeric_limits<float>::max() };
		glm::vec3 Max{ std::numeric_limits<float>::lowest() };

		void Expand(const glm::vec3& point)
		{
			Min = glm::min(Min, point);
			Max = glm::max(Max, point);
		}
		bool IsValid() const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
		glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }
		glm::vec3 GetExtents() const { return (Max - Min) * 0.5f; }
	};

	// CPU-side mesh: node transforms of the source file are baked into the vertices,
	// so one MeshData is a single object made of material-separated submeshes.
	// Files with skins or animations also carry a skeleton: every vertex is then bound to
	// palette joints (rigid nodes to their own node) and the clips animate the skeleton.
	struct MeshData
	{
		std::string Name;
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
		std::vector<Submesh> Submeshes;
		std::vector<MaterialData> Materials;
		std::vector<TextureData> Textures;
		BoundingBox Bounds; // bind pose

		// Skeletal data (empty for static meshes).
		Penumbra::Skeleton Skeleton;
		std::vector<SkinJoint> Joints;     // skinning palette
		std::vector<SkinVertex> SkinVertices; // parallel to Vertices when skinned
		std::vector<AnimationClip> Animations;

		bool IsSkinned() const { return !Joints.empty() && SkinVertices.size() == Vertices.size(); }
		// Returns nullptr when there is no clip with that name.
		const AnimationClip* FindAnimation(std::string_view name) const;

		void ComputeBounds();
		// Generates per-vertex tangents (MikkTSpace-style accumulation) from UVs.
		void ComputeTangents();
	};

}
