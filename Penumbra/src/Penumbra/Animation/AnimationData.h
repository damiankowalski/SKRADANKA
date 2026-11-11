#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// CPU-side skeletal animation data imported from glTF files (see GltfImporter).

namespace Penumbra {

	// Local transform of one node of a skeleton.
	struct NodeTransform
	{
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
	};

	// Local transforms of every node of a skeleton, indexed like Skeleton::Nodes.
	using Pose = std::vector<NodeTransform>;

	struct SkeletonNode
	{
		std::string Name;
		int32_t Parent = -1; // index into Skeleton::Nodes, always smaller than the node's own index
		NodeTransform Rest;  // local transform of the node in the source file
	};

	// The node hierarchy of an imported file. Parents come before their children, so global
	// transforms can be computed in a single forward pass.
	struct Skeleton
	{
		std::vector<SkeletonNode> Nodes;

		// Returns -1 when no node has that name.
		int32_t FindNode(std::string_view name) const;
	};

	// One entry of the skinning palette: vertices reference joints by their palette index.
	struct SkinJoint
	{
		uint32_t Node = 0;              // skeleton node driving the joint
		glm::mat4 InverseBind{ 1.0f };  // mesh space -> joint space at bind time
		glm::vec3 BoundsMin{ 0.0f };    // bind-space bounds of the vertices the joint influences
		glm::vec3 BoundsMax{ 0.0f };
		bool HasBounds = false;
	};

	// Per-vertex skinning data, parallel to MeshData::Vertices. 32 bytes, mirrored in Skinning.hlsl.
	struct SkinVertex
	{
		glm::uvec4 Joints{ 0u };
		glm::vec4 Weights{ 1.0f, 0.0f, 0.0f, 0.0f };
	};
	static_assert(sizeof(SkinVertex) == 32, "SkinVertex must match Skinning.hlsl");

	enum class AnimationPath : uint8_t
	{
		Translation = 0,
		Rotation,
		Scale
	};

	enum class AnimationInterpolation : uint8_t
	{
		Step = 0,
		Linear,
		CubicSpline
	};

	// Keyframes of one node property. Values hold xyz (translation/scale) or xyzw (rotation quaternion).
	// Cubic-spline channels store three values per key: in-tangent, value, out-tangent (glTF layout).
	struct AnimationChannel
	{
		uint32_t Node = 0;
		AnimationPath Path = AnimationPath::Translation;
		AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
		std::vector<float> Times;
		std::vector<glm::vec4> Values;
	};

	struct AnimationClip
	{
		std::string Name;
		float Duration = 0.0f; // seconds (last keyframe time)
		std::vector<AnimationChannel> Channels;
	};

}
