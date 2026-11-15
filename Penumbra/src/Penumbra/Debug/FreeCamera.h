#pragma once

#include "Penumbra/Renderer/RenderCamera.h"

#include <glm/glm.hpp>

namespace Penumbra {

	// What moves the free camera this frame (read from the keyboard and mouse by the caller).
	struct FreeCameraInput
	{
		glm::vec3 Move{ 0.0f };   // x right, y up, z forward; each -1..1
		glm::vec2 Look{ 0.0f };   // mouse movement in pixels (x right, y down)
		bool Fast = false;        // Shift: four times the speed
		float SpeedSteps = 0.0f;  // mouse wheel: each step changes the speed by 20%
	};

	// The runtime's freelook camera (debug panel, F5): detached from the player, it flies through
	// the level while the game goes on. Yaw turns about +Y (0 looks down -Z), pitch about the
	// camera's right axis; the field of view and clip planes come from the camera it started from.
	class FreeCamera
	{
	public:
		// Starts exactly where the given camera is, looking the same way.
		void StartFrom(const RenderCamera& camera);
		void SetPose(const glm::vec3& position, float yawDegrees, float pitchDegrees);
		void Update(const FreeCameraInput& input, float dt);

		RenderCamera GetRenderCamera(float aspect) const;
		glm::vec3 GetPosition() const { return m_Position; }
		glm::quat GetRotation() const;
		float GetYaw() const { return m_Yaw; }
		float GetPitch() const { return m_Pitch; }
		float GetSpeed() const { return m_Speed; }
		void SetSpeed(float speed);

		static constexpr float LookSensitivity = 0.15f; // degrees per pixel

	private:
		glm::vec3 m_Position{ 0.0f, 2.0f, 5.0f };
		float m_Yaw = 0.0f;
		float m_Pitch = 0.0f;
		float m_Speed = 6.0f; // m/s
		float m_VerticalFov = 60.0f;
		float m_NearClip = 0.05f;
		float m_FarClip = 500.0f;
	};

	// Direction (normalized, world space) of the ray from the camera through a pixel of a
	// viewport of the given size (top-left origin).
	glm::vec3 ScreenRayDirection(const RenderCamera& camera, const glm::vec2& pixel, const glm::vec2& viewportSize);
	// Pixel position of a world point (top-left origin); false when it is behind the camera.
	bool WorldToScreen(const RenderCamera& camera, const glm::vec3& point, const glm::vec2& viewportSize, glm::vec2& pixel);

}
