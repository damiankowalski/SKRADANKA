#include "pnpch.h"
#include "Penumbra/Debug/FreeCamera.h"

#include "Penumbra/Math/Math.h"

namespace Penumbra {

	void FreeCamera::StartFrom(const RenderCamera& camera)
	{
		const glm::mat4 world = glm::inverse(camera.View);
		const glm::vec3 forward = glm::normalize(-glm::vec3(world[2]));
		m_Position = glm::vec3(world[3]);
		m_Yaw = glm::degrees(std::atan2(-forward.x, -forward.z));
		m_Pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
		m_VerticalFov = camera.VerticalFOV;
		m_NearClip = camera.NearClip;
		m_FarClip = camera.FarClip;
	}

	void FreeCamera::SetPose(const glm::vec3& position, float yawDegrees, float pitchDegrees)
	{
		m_Position = position;
		m_Yaw = yawDegrees;
		m_Pitch = std::clamp(pitchDegrees, -89.0f, 89.0f);
	}

	void FreeCamera::SetSpeed(float speed) { m_Speed = std::clamp(speed, 0.5f, 100.0f); }

	glm::quat FreeCamera::GetRotation() const
	{
		return glm::angleAxis(glm::radians(m_Yaw), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(glm::radians(m_Pitch), glm::vec3(1.0f, 0.0f, 0.0f));
	}

	void FreeCamera::Update(const FreeCameraInput& input, float dt)
	{
		if (input.SpeedSteps != 0.0f)
			SetSpeed(m_Speed * std::pow(1.2f, input.SpeedSteps));
		m_Yaw -= input.Look.x * LookSensitivity;
		m_Pitch = std::clamp(m_Pitch - input.Look.y * LookSensitivity, -89.0f, 89.0f);

		const glm::quat rotation = GetRotation();
		const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
		glm::vec3 move = right * input.Move.x + glm::vec3(0.0f, 1.0f, 0.0f) * input.Move.y + forward * input.Move.z;
		if (glm::length(move) > 1.0f)
			move = glm::normalize(move);
		m_Position += move * m_Speed * (input.Fast ? 4.0f : 1.0f) * dt;
	}

	RenderCamera FreeCamera::GetRenderCamera(float aspect) const
	{
		return RenderCamera::Perspective(m_Position, GetRotation(), m_VerticalFov, aspect, m_NearClip, m_FarClip);
	}

	glm::vec3 ScreenRayDirection(const RenderCamera& camera, const glm::vec2& pixel, const glm::vec2& viewportSize)
	{
		const glm::vec2 size = glm::max(viewportSize, glm::vec2(1.0f));
		const glm::vec2 ndc(pixel.x / size.x * 2.0f - 1.0f, 1.0f - pixel.y / size.y * 2.0f);
		const glm::mat4 inverse = glm::inverse(camera.Projection * camera.View);
		glm::vec4 nearPoint = inverse * glm::vec4(ndc, 0.0f, 1.0f);
		glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;
		return glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
	}

	bool WorldToScreen(const RenderCamera& camera, const glm::vec3& point, const glm::vec2& viewportSize, glm::vec2& pixel)
	{
		const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(point, 1.0f);
		if (clip.w <= 1e-4f)
			return false;
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		pixel = glm::vec2((ndc.x * 0.5f + 0.5f) * viewportSize.x, (0.5f - ndc.y * 0.5f) * viewportSize.y);
		return true;
	}

}
