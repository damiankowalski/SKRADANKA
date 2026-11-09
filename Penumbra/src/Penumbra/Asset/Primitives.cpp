#include "pnpch.h"
#include "Penumbra/Asset/Primitives.h"

#include <glm/gtc/constants.hpp>

// All primitives use counter-clockwise front faces (glTF convention).

namespace Penumbra::Primitives {

	namespace {

		void Finalize(MeshData& mesh, const char* name)
		{
			mesh.Name = name;
			mesh.Materials = { MaterialData{ .Name = "Default" } };
			mesh.Submeshes = { Submesh{ 0, static_cast<uint32_t>(mesh.Indices.size()), 0 } };
			mesh.ComputeTangents();
			mesh.ComputeBounds();
		}

		// Adds a quad facing normal; u x v must equal normal.
		void AddFace(MeshData& mesh, const glm::vec3& center, const glm::vec3& normal, const glm::vec3& u, const glm::vec3& v,
			const glm::vec2& size)
		{
			const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
			const glm::vec2 corners[4] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
			for (const glm::vec2& corner : corners)
			{
				Vertex vertex;
				vertex.Position = center + u * (corner.x * size.x) + v * (corner.y * size.y);
				vertex.Normal = normal;
				vertex.TexCoord = glm::vec2(corner.x + 0.5f, 0.5f - corner.y);
				mesh.Vertices.push_back(vertex);
			}
			for (uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u })
				mesh.Indices.push_back(base + index);
		}

		// Connects two rows of (segments + 1) vertices; rowA is above rowB.
		void ConnectRows(MeshData& mesh, uint32_t rowA, uint32_t rowB, uint32_t segments)
		{
			for (uint32_t s = 0; s < segments; s++)
			{
				const uint32_t a = rowA + s, d = rowA + s + 1;
				const uint32_t b = rowB + s, c = rowB + s + 1;
				mesh.Indices.insert(mesh.Indices.end(), { a, d, c, a, c, b });
			}
		}

		uint32_t AddSphereRow(MeshData& mesh, float phi, float yOffset, float v, uint32_t segments, float radius)
		{
			const uint32_t start = static_cast<uint32_t>(mesh.Vertices.size());
			for (uint32_t s = 0; s <= segments; s++)
			{
				const float theta = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(segments);
				const glm::vec3 direction(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
				Vertex vertex;
				vertex.Position = direction * radius + glm::vec3(0.0f, yOffset, 0.0f);
				vertex.Normal = direction;
				vertex.TexCoord = glm::vec2(static_cast<float>(s) / static_cast<float>(segments), v);
				mesh.Vertices.push_back(vertex);
			}
			return start;
		}

	}

	MeshData CreateCube()
	{
		MeshData mesh;
		const glm::vec2 size(1.0f);
		AddFace(mesh, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, size);
		AddFace(mesh, { -0.5f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, size);
		AddFace(mesh, { 0.0f, 0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, size);
		AddFace(mesh, { 0.0f, -0.5f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, size);
		AddFace(mesh, { 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, size);
		AddFace(mesh, { 0.0f, 0.0f, -0.5f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, size);
		Finalize(mesh, "Cube");
		return mesh;
	}

	MeshData CreateSphere(uint32_t segments, uint32_t rings)
	{
		MeshData mesh;
		uint32_t previous = 0;
		for (uint32_t r = 0; r <= rings; r++)
		{
			const float v = static_cast<float>(r) / static_cast<float>(rings);
			const uint32_t row = AddSphereRow(mesh, glm::pi<float>() * v, 0.0f, v, segments, 0.5f);
			if (r > 0)
				ConnectRows(mesh, previous, row, segments);
			previous = row;
		}
		Finalize(mesh, "Sphere");
		return mesh;
	}

	MeshData CreatePlane()
	{
		MeshData mesh;
		AddFace(mesh, glm::vec3(0.0f), { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, glm::vec2(1.0f));
		Finalize(mesh, "Plane");
		return mesh;
	}

	MeshData CreateCapsule(uint32_t segments, uint32_t rings)
	{
		MeshData mesh;
		const uint32_t half = std::max(rings / 2, 2u);
		uint32_t previous = 0;
		bool first = true;
		for (uint32_t hemisphere = 0; hemisphere < 2; hemisphere++)
		{
			const float yOffset = hemisphere == 0 ? 0.5f : -0.5f;
			for (uint32_t i = 0; i <= half; i++)
			{
				const float t = static_cast<float>(i) / static_cast<float>(half);
				const float phi = glm::half_pi<float>() * (static_cast<float>(hemisphere) + t);
				const float v = (static_cast<float>(hemisphere) + t) * 0.5f;
				const uint32_t row = AddSphereRow(mesh, phi, yOffset, v, segments, 0.5f);
				if (!first)
					ConnectRows(mesh, previous, row, segments);
				previous = row;
				first = false;
			}
		}
		Finalize(mesh, "Capsule");
		return mesh;
	}

	MeshData CreateCylinder(uint32_t segments)
	{
		MeshData mesh;
		auto addRing = [&](float y, const glm::vec3* capNormal, float v) {
			const uint32_t start = static_cast<uint32_t>(mesh.Vertices.size());
			for (uint32_t s = 0; s <= segments; s++)
			{
				const float theta = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(segments);
				const glm::vec3 direction(std::cos(theta), 0.0f, std::sin(theta));
				Vertex vertex;
				vertex.Position = direction * 0.5f + glm::vec3(0.0f, y, 0.0f);
				vertex.Normal = capNormal ? *capNormal : direction;
				vertex.TexCoord = capNormal ? glm::vec2(direction.x, direction.z) * 0.5f + 0.5f
											: glm::vec2(static_cast<float>(s) / static_cast<float>(segments), v);
				mesh.Vertices.push_back(vertex);
			}
			return start;
		};

		const uint32_t top = addRing(0.5f, nullptr, 0.0f);
		const uint32_t bottom = addRing(-0.5f, nullptr, 1.0f);
		ConnectRows(mesh, top, bottom, segments);

		const glm::vec3 up(0.0f, 1.0f, 0.0f), down(0.0f, -1.0f, 0.0f);
		for (int cap = 0; cap < 2; cap++)
		{
			const bool isTop = cap == 0;
			const float y = isTop ? 0.5f : -0.5f;
			const uint32_t ring = addRing(y, isTop ? &up : &down, 0.0f);
			const uint32_t center = static_cast<uint32_t>(mesh.Vertices.size());
			mesh.Vertices.push_back(Vertex{ { 0.0f, y, 0.0f }, isTop ? up : down, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.5f, 0.5f } });
			for (uint32_t s = 0; s < segments; s++)
			{
				if (isTop)
					mesh.Indices.insert(mesh.Indices.end(), { center, ring + s + 1, ring + s });
				else
					mesh.Indices.insert(mesh.Indices.end(), { center, ring + s, ring + s + 1 });
			}
		}
		Finalize(mesh, "Cylinder");
		return mesh;
	}

	std::shared_ptr<const MeshData> GetBuiltin(std::string_view name)
	{
		if (name.starts_with(BuiltinPrefix))
			name.remove_prefix(BuiltinPrefix.size());

		static std::mutex s_Mutex;
		static std::unordered_map<std::string, std::shared_ptr<const MeshData>> s_Cache;
		std::scoped_lock lock(s_Mutex);
		const std::string key(name);
		if (const auto it = s_Cache.find(key); it != s_Cache.end())
			return it->second;

		std::shared_ptr<const MeshData> mesh;
		if (name == "cube")
			mesh = std::make_shared<const MeshData>(CreateCube());
		else if (name == "sphere")
			mesh = std::make_shared<const MeshData>(CreateSphere());
		else if (name == "plane")
			mesh = std::make_shared<const MeshData>(CreatePlane());
		else if (name == "capsule")
			mesh = std::make_shared<const MeshData>(CreateCapsule());
		else if (name == "cylinder")
			mesh = std::make_shared<const MeshData>(CreateCylinder());
		else
			return nullptr;

		s_Cache[key] = mesh;
		return mesh;
	}

}
