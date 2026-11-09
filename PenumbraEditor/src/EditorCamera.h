#pragma once

#include "Penumbra/Renderer/RenderCamera.h"

#include <glm/glm.hpp>

namespace Penumbra {

	// Free-fly editor camera: hold the right mouse button to look around and move with
	// WASD/QE (Shift = fast), mouse wheel changes speed; Alt+left drag orbits; F frames a point.
	class EditorCamera
	{
	public:
		void Update(float dt, bool viewportHovered, bool viewportFocused);
		void Focus(const glm::vec3& target, float distance);

		RenderCamera GetRenderCamera(float aspect) const;
		glm::vec3 GetPosition() const { return m_Position; }
		glm::quat GetRotation() const;
		void SetPose(const glm::vec3& position, float yawDegrees, float pitchDegrees);

	private:
		glm::vec3 m_Position{ 6.0f, 5.0f, 10.0f };
		float m_Yaw = 30.0f;    // degrees, around +Y
		float m_Pitch = -20.0f; // degrees
		float m_Speed = 6.0f;
		glm::vec3 m_OrbitTarget{ 0.0f };
		bool m_Flying = false;
	};

}
