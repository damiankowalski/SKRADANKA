#pragma once

#include "Penumbra/Asset/MeshData.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>

namespace Penumbra {

	class Scene;
	class Entity;

	struct RenderCamera
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::vec3 Position{ 0.0f };
		float NearClip = 0.05f;
		float FarClip = 500.0f;
		float VerticalFOV = 60.0f; // degrees
		float AspectRatio = 16.0f / 9.0f;

		// D3D depth range [0, 1], right-handed view space (camera looks down -Z).
		static RenderCamera Perspective(const glm::vec3& position, const glm::quat& rotation, float verticalFovDegrees, float aspect,
			float nearClip, float farClip);
		// The scene's primary camera (or a default view when the scene has none).
		static RenderCamera FromScene(Scene& scene, float aspect);
	};

	// Six planes (inward normals) extracted from a view-projection matrix.
	class Frustum
	{
	public:
		explicit Frustum(const glm::mat4& viewProjection);

		bool IntersectsSphere(const glm::vec3& center, float radius) const;
		bool IntersectsBox(const BoundingBox& localBox, const glm::mat4& transform) const;

	private:
		std::array<glm::vec4, 6> m_Planes;
	};

}
