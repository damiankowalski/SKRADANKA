#pragma once

#include "Penumbra/Core/Base.h"

#include <glm/glm.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;

namespace Penumbra {

	struct NavMeshBuildSettings
	{
		float CellSize = 0.15f;
		float CellHeight = 0.1f;
		float AgentRadius = 0.35f;
		float AgentHeight = 1.8f;
		float AgentMaxClimb = 0.4f;
		float AgentMaxSlope = 45.0f;
	};

	struct NavPath
	{
		std::vector<glm::vec3> Points; // corners from the start to the end, both included
		bool Complete = true;          // false: the goal is unreachable, the path ends at the closest point
		float Length = 0.0f;
	};

	// A single-tile Recast navmesh built from a triangle soup, with the Detour queries the
	// game needs. Pure geometry: no scene access, so it is unit-testable on its own.
	class NavMesh
	{
	public:
		// How far around a query point the nearest polygon is searched for (half extents).
		static constexpr glm::vec3 DefaultSearchExtents{ 1.5f, 3.0f, 1.5f };

		NavMesh();
		~NavMesh();

		NavMesh(const NavMesh&) = delete;
		NavMesh& operator=(const NavMesh&) = delete;

		// Builds the navmesh. Returns false with a message when the input has no walkable
		// surface or exceeds the limits of a single tile (65k vertices).
		bool Build(std::span<const glm::vec3> vertices, std::span<const uint32_t> indices, const NavMeshBuildSettings& settings,
			std::string& error);
		bool IsValid() const { return m_NavMesh != nullptr; }
		uint32_t GetPolygonCount() const { return m_PolygonCount; }

		// Closest point on the navmesh within the search extents.
		std::optional<glm::vec3> ClosestPoint(const glm::vec3& point, const glm::vec3& searchExtents = DefaultSearchExtents) const;
		// Shortest path between two points. std::nullopt when either end is off the navmesh;
		// an unreachable goal gives a partial path (Complete = false) to the closest reachable point.
		std::optional<NavPath> FindPath(const glm::vec3& from, const glm::vec3& to) const;
		// Uniformly random reachable point within radius of the centre (deterministic for a given
		// random state, which is advanced).
		std::optional<glm::vec3> RandomPointAround(const glm::vec3& center, float radius, uint32_t& randomState) const;

		// Polygon outlines for debug drawing (pairs of points).
		std::vector<glm::vec3> GetDebugEdges() const;

	private:
		void Release();

	private:
		dtNavMesh* m_NavMesh = nullptr;
		dtNavMeshQuery* m_Query = nullptr;
		uint32_t m_PolygonCount = 0;
	};

}
