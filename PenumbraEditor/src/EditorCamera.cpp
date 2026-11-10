#include "EditorCamera.h"

#include "Penumbra/Core/Input.h"
#include "Penumbra/Math/Math.h"

#include <algorithm>

namespace Penumbra {

	glm::quat EditorCamera::GetRotation() const
	{
		return Math::QuatFromEulerDegrees(glm::vec3(m_Pitch, m_Yaw, 0.0f));
	}

	void EditorCamera::SetPose(const glm::vec3& position, float yawDegrees, float pitchDegrees)
	{
		m_Position = position;
		m_Yaw = yawDegrees;
		m_Pitch = std::clamp(pitchDegrees, -89.0f, 89.0f);
	}

	void EditorCamera::Focus(const glm::vec3& target, float distance)
	{
		m_OrbitTarget = target;
		m_Position = target - Math::Forward(GetRotation()) * std::max(distance, 1.0f);
	}

	void EditorCamera::Update(float dt, bool viewportHovered, bool viewportFocused)
	{
		const glm::vec2 delta = Input::GetMouseDelta();
		const bool rightDown = Input::IsMouseButtonDown(Mouse::ButtonRight);

		if (rightDown && (viewportHovered || m_Flying))
		{
			m_Flying = true;
			m_Yaw -= delta.x * 0.15f;
			m_Pitch = std::clamp(m_Pitch - delta.y * 0.15f, -89.0f, 89.0f);

			const glm::quat rotation = GetRotation();
			glm::vec3 move(0.0f);
			if (Input::IsKeyDown(Key::W))
				move += Math::Forward(rotation);
			if (Input::IsKeyDown(Key::S))
				move -= Math::Forward(rotation);
			if (Input::IsKeyDown(Key::D))
				move += Math::Right(rotation);
			if (Input::IsKeyDown(Key::A))
				move -= Math::Right(rotation);
			if (Input::IsKeyDown(Key::E))
				move += glm::vec3(0.0f, 1.0f, 0.0f);
			if (Input::IsKeyDown(Key::Q))
				move -= glm::vec3(0.0f, 1.0f, 0.0f);
			const float scroll = Input::GetScrollDelta();
			if (scroll != 0.0f)
				m_Speed = std::clamp(m_Speed * (scroll > 0.0f ? 1.2f : 1.0f / 1.2f), 0.5f, 200.0f);
			const float speed = m_Speed * (Input::IsKeyDown(Key::LeftShift) ? 3.0f : 1.0f);
			if (glm::dot(move, move) > 0.0f)
				m_Position += glm::normalize(move) * speed * dt;
			m_OrbitTarget = m_Position + Math::Forward(rotation) * 5.0f;
			return;
		}
		m_Flying = false;

		if (!viewportHovered)
			return;

		// Alt + left drag: orbit around the last focus point.
		if (Input::IsKeyDown(Key::LeftAlt) && Input::IsMouseButtonDown(Mouse::ButtonLeft))
		{
			const float distance = glm::length(m_Position - m_OrbitTarget);
			m_Yaw -= delta.x * 0.25f;
			m_Pitch = std::clamp(m_Pitch - delta.y * 0.25f, -89.0f, 89.0f);
			m_Position = m_OrbitTarget - Math::Forward(GetRotation()) * distance;
		}

		// Wheel without the right button: dolly.
		const float scroll = Input::GetScrollDelta();
		if (scroll != 0.0f && viewportFocused)
			m_Position += Math::Forward(GetRotation()) * scroll * std::max(m_Speed * 0.25f, 0.5f);
	}

	RenderCamera EditorCamera::GetRenderCamera(float aspect) const
	{
		return RenderCamera::Perspective(m_Position, GetRotation(), 60.0f, aspect, 0.05f, 1000.0f);
	}

}
