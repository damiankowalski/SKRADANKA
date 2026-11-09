#include "pnpch.h"
#include "Penumbra/Asset/MeshData.h"

namespace Penumbra {

	void MeshData::ComputeBounds()
	{
		Bounds = BoundingBox{};
		for (const Vertex& vertex : Vertices)
			Bounds.Expand(vertex.Position);
	}

	void MeshData::ComputeTangents()
	{
		std::vector<glm::vec3> tangents(Vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangents(Vertices.size(), glm::vec3(0.0f));

		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			const uint32_t i0 = Indices[i], i1 = Indices[i + 1], i2 = Indices[i + 2];
			const Vertex& v0 = Vertices[i0];
			const Vertex& v1 = Vertices[i1];
			const Vertex& v2 = Vertices[i2];

			const glm::vec3 edge1 = v1.Position - v0.Position;
			const glm::vec3 edge2 = v2.Position - v0.Position;
			const glm::vec2 duv1 = v1.TexCoord - v0.TexCoord;
			const glm::vec2 duv2 = v2.TexCoord - v0.TexCoord;
			const float determinant = duv1.x * duv2.y - duv2.x * duv1.y;
			if (std::abs(determinant) < 1e-12f)
				continue;
			const float inverse = 1.0f / determinant;
			const glm::vec3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * inverse;
			const glm::vec3 bitangent = (edge2 * duv1.x - edge1 * duv2.x) * inverse;
			for (uint32_t index : { i0, i1, i2 })
			{
				tangents[index] += tangent;
				bitangents[index] += bitangent;
			}
		}

		for (size_t i = 0; i < Vertices.size(); i++)
		{
			const glm::vec3& normal = Vertices[i].Normal;
			glm::vec3 tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);
			if (glm::dot(tangent, tangent) < 1e-12f)
			{
				// Degenerate UVs: pick any vector perpendicular to the normal.
				tangent = std::abs(normal.x) < 0.9f ? glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f)) : glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
			}
			tangent = glm::normalize(tangent);
			const float handedness = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
			Vertices[i].Tangent = glm::vec4(tangent, handedness);
		}
	}

}
