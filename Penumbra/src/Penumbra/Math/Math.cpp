#include "pnpch.h"
#include "Penumbra/Math/Math.h"

#include <glm/gtx/matrix_decompose.hpp>

namespace Penumbra::Math {

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
	{
		return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
	}

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		if (!glm::decompose(transform, scale, rotation, translation, skew, perspective))
			return false;
		rotation = glm::normalize(rotation);
		return true;
	}

	glm::quat QuatFromEulerDegrees(const glm::vec3& degrees)
	{
		return glm::normalize(glm::quat(glm::radians(degrees)));
	}

	glm::vec3 EulerDegreesFromQuat(const glm::quat& rotation)
	{
		return glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
	}

	glm::quat LookRotation(const glm::vec3& direction, const glm::vec3& up)
	{
		const float length = glm::length(direction);
		if (length < Epsilon)
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		const glm::vec3 forward = direction / length;
		glm::vec3 upHint = up;
		if (std::abs(glm::dot(forward, glm::normalize(upHint))) > 0.999f)
			upHint = std::abs(forward.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		return glm::normalize(glm::quatLookAtRH(forward, upHint));
	}

	float SegmentSphereOverlap(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec3& center, float radius)
	{
		const glm::vec3 toOrigin = origin - center;
		const float b = glm::dot(toOrigin, direction);
		const float c = glm::dot(toOrigin, toOrigin) - radius * radius;
		const float discriminant = b * b - c;
		if (discriminant <= 0.0f)
			return 0.0f;
		const float root = std::sqrt(discriminant);
		const float enter = std::max(-b - root, 0.0f);
		const float exit = std::min(-b + root, length);
		return std::max(exit - enter, 0.0f);
	}

}
