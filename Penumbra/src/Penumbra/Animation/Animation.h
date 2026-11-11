#pragma once

#include "Penumbra/Asset/MeshData.h"

#include <vector>

// Pure skeletal animation math: keyframe sampling, pose blending and skinning matrices.
// No scene or GPU dependencies, so everything here is unit tested directly.

namespace Penumbra::Animation {

	// The rest pose of a skeleton (the node transforms stored in the file).
	Pose RestPose(const Skeleton& skeleton);

	// Wraps (loop) or clamps a playback time into [0, duration].
	float WrapTime(float time, float duration, bool loop);

	// Samples one channel at the given time (clamped to its key range). Rotations are normalized.
	glm::vec4 SampleChannel(const AnimationChannel& channel, float time);

	// Overwrites the animated properties of the pose with the clip sampled at the given time.
	// Nodes the clip does not animate keep their values.
	void SampleClip(const AnimationClip& clip, float time, Pose& pose);

	// out = lerp(from, to, weight) per node (rotations via shortest-path normalized lerp).
	// All three poses must have the same size; out may alias either input.
	void BlendPoses(const Pose& from, const Pose& to, float weight, Pose& out);

	glm::mat4 ToMatrix(const NodeTransform& transform);

	// Global (mesh space) transforms of every node of the skeleton.
	void ComputeGlobalTransforms(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& globals);

	// Skinning palette: joint i maps bind-space vertices to their posed position (global * inverse bind).
	void ComputeSkinMatrices(const MeshData& mesh, const std::vector<glm::mat4>& globals, std::vector<glm::mat4>& skinMatrices);

	// Conservative mesh-space bounds of the skinned mesh, from each joint's bind-space bounds.
	BoundingBox ComputeSkinnedBounds(const MeshData& mesh, const std::vector<glm::mat4>& skinMatrices);

	// CPU reference of the GPU skinning shader (used by tests and tools).
	Vertex SkinVertexCPU(const Vertex& vertex, const SkinVertex& skin, const std::vector<glm::mat4>& skinMatrices);

}
