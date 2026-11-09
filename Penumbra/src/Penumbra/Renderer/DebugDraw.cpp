#include "pnpch.h"
#include "Penumbra/Renderer/DebugDraw.h"

#include <glm/gtc/constants.hpp>

namespace Penumbra {

	void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
	{
		m_Vertices.push_back({ from, color });
		m_Vertices.push_back({ to, color });
	}

	void DebugDraw::Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const glm::vec4& color)
	{
		glm::vec3 corners[8];
		for (int i = 0; i < 8; i++)
		{
			const glm::vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
			corners[i] = center + rotation * (sign * halfExtents);
		}
		static constexpr int Edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for (const auto& edge : Edges)
			Line(corners[edge[0]], corners[edge[1]], color);
	}

	void DebugDraw::Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int segments)
	{
		const glm::vec3 n = glm::normalize(normal);
		const glm::vec3 tangent = glm::normalize(glm::cross(n, std::abs(n.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
		const glm::vec3 bitangent = glm::cross(n, tangent);
		glm::vec3 previous = center + tangent * radius;
		for (int i = 1; i <= segments; i++)
		{
			const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
			const glm::vec3 next = center + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
			Line(previous, next, color);
			previous = next;
		}
	}

	void DebugDraw::Sphere(const glm::vec3& center, float radius, const glm::vec4& color)
	{
		Circle(center, glm::vec3(1, 0, 0), radius, color);
		Circle(center, glm::vec3(0, 1, 0), radius, color);
		Circle(center, glm::vec3(0, 0, 1), radius, color);
	}

	void DebugDraw::Cone(const glm::vec3& apex, const glm::vec3& direction, float length, float halfAngleDegrees, const glm::vec4& color)
	{
		const glm::vec3 forward = glm::normalize(direction);
		const float radius = std::tan(glm::radians(std::clamp(halfAngleDegrees, 0.0f, 89.0f))) * length;
		const glm::vec3 baseCenter = apex + forward * length;
		Circle(baseCenter, forward, radius, color, 24);
		const glm::vec3 tangent = glm::normalize(glm::cross(forward, std::abs(forward.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
		const glm::vec3 bitangent = glm::cross(forward, tangent);
		for (const glm::vec3& side : { tangent, -tangent, bitangent, -bitangent })
			Line(apex, baseCenter + side * radius, color);
	}

	void DebugDraw::Grid(float halfSize, float spacing, const glm::vec4& color)
	{
		const int lines = static_cast<int>(halfSize / std::max(spacing, 0.01f));
		for (int i = -lines; i <= lines; i++)
		{
			const float offset = static_cast<float>(i) * spacing;
			const glm::vec4 lineColor = i == 0 ? glm::vec4(color.r * 1.6f, color.g * 1.6f, color.b * 1.6f, color.a) : color;
			Line({ offset, 0.0f, -halfSize }, { offset, 0.0f, halfSize }, lineColor);
			Line({ -halfSize, 0.0f, offset }, { halfSize, 0.0f, offset }, lineColor);
		}
	}

}
