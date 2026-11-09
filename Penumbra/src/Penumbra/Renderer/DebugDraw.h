#pragma once

#include "Penumbra/Renderer/RendererTypes.h"

#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Penumbra {

	// World-space debug lines drawn on top of the frame (depth tested). Collected every frame
	// by the editor / gameplay and cleared after rendering.
	class DebugDraw
	{
	public:
		void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
		void Box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const glm::vec4& color);
		void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, int segments = 32);
		void Sphere(const glm::vec3& center, float radius, const glm::vec4& color);
		// View cone: apex, forward direction, length and half-angle in degrees.
		void Cone(const glm::vec3& apex, const glm::vec3& direction, float length, float halfAngleDegrees, const glm::vec4& color);
		void Grid(float halfSize, float spacing, const glm::vec4& color);

		const std::vector<GPU::OverlayVertex>& GetVertices() const { return m_Vertices; }
		void Clear() { m_Vertices.clear(); }

	private:
		std::vector<GPU::OverlayVertex> m_Vertices;
	};

}
