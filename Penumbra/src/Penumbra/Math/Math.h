#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Penumbra::Math {

	constexpr float Epsilon = 1e-6f;

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale);
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale);

	// Euler angles in degrees (pitch X, yaw Y, roll Z) - the representation used by
	// scene files, the editor and the scripting API because it is human-readable.
	glm::quat QuatFromEulerDegrees(const glm::vec3& degrees);
	glm::vec3 EulerDegreesFromQuat(const glm::quat& rotation);

	// Engine convention: right-handed, Y up, forward is -Z.
	inline glm::vec3 Forward(const glm::quat& rotation) { return rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
	inline glm::vec3 Right(const glm::quat& rotation) { return rotation * glm::vec3(1.0f, 0.0f, 0.0f); }
	inline glm::vec3 Up(const glm::quat& rotation) { return rotation * glm::vec3(0.0f, 1.0f, 0.0f); }

	// Rotation that makes Forward() point along direction (with +Y as up hint).
	glm::quat LookRotation(const glm::vec3& direction, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

	// Rec. 709 relative luminance of a linear RGB color.
	inline float Luminance(const glm::vec3& color) { return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }

	// Ray / sphere intersection length (0 when the segment misses the sphere).
	float SegmentSphereOverlap(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec3& center, float radius);

}
