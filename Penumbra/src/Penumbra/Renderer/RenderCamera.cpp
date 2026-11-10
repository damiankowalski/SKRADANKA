#include "pnpch.h"
#include "Penumbra/Renderer/RenderCamera.h"

#include "Penumbra/Scene/Entity.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Penumbra {

	RenderCamera RenderCamera::Perspective(const glm::vec3& position, const glm::quat& rotation, float verticalFovDegrees, float aspect,
		float nearClip, float farClip)
	{
		RenderCamera camera;
		camera.Position = position;
		camera.NearClip = std::max(nearClip, 0.001f);
		camera.FarClip = std::max(farClip, camera.NearClip + 0.01f);
		camera.VerticalFOV = std::clamp(verticalFovDegrees, 1.0f, 179.0f);
		camera.AspectRatio = std::max(aspect, 0.01f);
		camera.View = glm::inverse(Math::ComposeTransform(position, rotation, glm::vec3(1.0f)));
		camera.Projection = glm::perspectiveRH_ZO(glm::radians(camera.VerticalFOV), camera.AspectRatio, camera.NearClip, camera.FarClip);
		return camera;
	}

	RenderCamera RenderCamera::FromScene(Scene& scene, float aspect)
	{
		Entity cameraEntity = scene.GetPrimaryCameraEntity();
		if (!cameraEntity)
			return Perspective(glm::vec3(0.0f, 2.0f, 5.0f), Math::LookRotation(glm::vec3(0.0f, -0.3f, -1.0f)), 60.0f, aspect, 0.05f, 500.0f);
		const auto& camera = cameraEntity.GetComponent<CameraComponent>();
		return Perspective(scene.GetWorldPosition(cameraEntity), scene.GetWorldRotation(cameraEntity), camera.VerticalFOV, aspect,
			camera.NearClip, camera.FarClip);
	}

	Frustum::Frustum(const glm::mat4& m)
	{
		auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
		const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
		m_Planes = { r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2 };
		for (glm::vec4& plane : m_Planes)
			plane /= std::max(glm::length(glm::vec3(plane)), 1e-8f);
	}

	bool Frustum::IntersectsSphere(const glm::vec3& center, float radius) const
	{
		for (const glm::vec4& plane : m_Planes)
		{
			if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
				return false;
		}
		return true;
	}

	bool Frustum::IntersectsBox(const BoundingBox& localBox, const glm::mat4& transform) const
	{
		if (!localBox.IsValid())
			return true;
		// World-space AABB of the transformed box (Arvo's method).
		const glm::vec3 center = glm::vec3(transform * glm::vec4(localBox.GetCenter(), 1.0f));
		const glm::vec3 extents = localBox.GetExtents();
		const glm::mat3 basis(transform);
		const glm::vec3 worldExtents = glm::abs(basis[0]) * extents.x + glm::abs(basis[1]) * extents.y + glm::abs(basis[2]) * extents.z;
		for (const glm::vec4& plane : m_Planes)
		{
			const glm::vec3 normal(plane);
			const float distance = glm::dot(normal, center) + plane.w;
			const float projected = glm::dot(glm::abs(normal), worldExtents);
			if (distance < -projected)
				return false;
		}
		return true;
	}

}
