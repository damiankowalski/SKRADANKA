#include "pnpch.h"
#include "Penumbra/Navigation/NavMesh.h"

#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <glm/gtc/constants.hpp>

namespace Penumbra {

	namespace {

		constexpr int MaxPathPolygons = 512;
		constexpr int MaxQueryNodes = 4096;
		constexpr unsigned short WalkableFlag = 0x01;
		constexpr int MaxVerticesPerPolygon = 6;

		struct RecastDeleter
		{
			void operator()(rcHeightfield* p) const { rcFreeHeightField(p); }
			void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); }
			void operator()(rcContourSet* p) const { rcFreeContourSet(p); }
			void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); }
			void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); }
		};
		template<typename T>
		using RecastPtr = std::unique_ptr<T, RecastDeleter>;

		// xorshift32: deterministic and good enough for picking search spots. Returns [0, 1).
		float NextRandom(uint32_t& state)
		{
			uint32_t x = state != 0 ? state : 0x9E3779B9u;
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			state = x;
			return static_cast<float>(x >> 8) / static_cast<float>(1u << 24);
		}

		const float* Ptr(const glm::vec3& v) { return &v.x; }
	}

	NavMesh::NavMesh() = default;

	NavMesh::~NavMesh()
	{
		Release();
	}

	void NavMesh::Release()
	{
		dtFreeNavMeshQuery(m_Query);
		dtFreeNavMesh(m_NavMesh);
		m_Query = nullptr;
		m_NavMesh = nullptr;
		m_PolygonCount = 0;
	}

	bool NavMesh::Build(std::span<const glm::vec3> vertices, std::span<const uint32_t> indices, const NavMeshBuildSettings& settings,
		std::string& error)
	{
		Release();
		if (vertices.empty() || indices.size() < 3)
		{
			error = "navmesh: no static geometry to build from";
			return false;
		}
		if (settings.CellSize <= 0.0f || settings.CellHeight <= 0.0f || settings.AgentHeight <= 0.0f || settings.AgentRadius < 0.0f)
		{
			error = "navmesh: cell sizes and agent height must be positive";
			return false;
		}

		const int vertexCount = static_cast<int>(vertices.size());
		const int triangleCount = static_cast<int>(indices.size() / 3);
		const float* vertexData = Ptr(vertices[0]);
		std::vector<int> triangles(indices.begin(), indices.begin() + static_cast<ptrdiff_t>(triangleCount) * 3);

		rcConfig config{};
		config.cs = settings.CellSize;
		config.ch = settings.CellHeight;
		config.walkableSlopeAngle = std::clamp(settings.AgentMaxSlope, 0.0f, 89.0f);
		config.walkableHeight = static_cast<int>(std::ceil(settings.AgentHeight / config.ch));
		config.walkableClimb = static_cast<int>(std::floor(settings.AgentMaxClimb / config.ch));
		config.walkableRadius = static_cast<int>(std::ceil(settings.AgentRadius / config.cs));
		config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
		config.maxSimplificationError = 1.3f;
		config.minRegionArea = 8 * 8;
		config.mergeRegionArea = 20 * 20;
		config.maxVertsPerPoly = MaxVerticesPerPolygon;
		config.detailSampleDist = config.cs * 6.0f < 0.9f ? 0.0f : config.cs * 6.0f;
		config.detailSampleMaxError = config.ch;
		rcCalcBounds(vertexData, vertexCount, config.bmin, config.bmax);
		rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
		if (config.width <= 0 || config.height <= 0)
		{
			error = "navmesh: degenerate geometry bounds";
			return false;
		}

		rcContext context(false);
		RecastPtr<rcHeightfield> solid(rcAllocHeightfield());
		if (!solid || !rcCreateHeightfield(&context, *solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
		{
			error = std::format("navmesh: cannot allocate a {}x{} heightfield (level too large for the cell size?)", config.width, config.height);
			return false;
		}

		std::vector<unsigned char> areas(static_cast<size_t>(triangleCount), RC_NULL_AREA);
		rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertexData, vertexCount, triangles.data(), triangleCount, areas.data());
		if (!rcRasterizeTriangles(&context, vertexData, vertexCount, triangles.data(), areas.data(), triangleCount, *solid, config.walkableClimb))
		{
			error = "navmesh: rasterization failed";
			return false;
		}
		rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *solid);
		rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *solid);
		rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *solid);

		RecastPtr<rcCompactHeightfield> compact(rcAllocCompactHeightfield());
		if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *solid, *compact))
		{
			error = "navmesh: cannot build the compact heightfield";
			return false;
		}
		solid.reset();
		if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) || !rcBuildDistanceField(&context, *compact) ||
			!rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea))
		{
			error = "navmesh: cannot build walkable regions";
			return false;
		}

		RecastPtr<rcContourSet> contours(rcAllocContourSet());
		if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours))
		{
			error = "navmesh: cannot build contours";
			return false;
		}
		RecastPtr<rcPolyMesh> polyMesh(rcAllocPolyMesh());
		if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh))
		{
			error = "navmesh: cannot build polygons";
			return false;
		}
		RecastPtr<rcPolyMeshDetail> detailMesh(rcAllocPolyMeshDetail());
		if (!detailMesh ||
			!rcBuildPolyMeshDetail(&context, *polyMesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detailMesh))
		{
			error = "navmesh: cannot build the detail mesh";
			return false;
		}
		if (polyMesh->npolys == 0)
		{
			error = "navmesh: no walkable surface found";
			return false;
		}
		if (polyMesh->nverts >= 0xffff)
		{
			error = std::format("navmesh: {} vertices exceed the single-tile limit; increase CellSize", polyMesh->nverts);
			return false;
		}

		for (int i = 0; i < polyMesh->npolys; ++i)
		{
			if (polyMesh->areas[i] == RC_WALKABLE_AREA)
				polyMesh->areas[i] = 0;
			polyMesh->flags[i] = WalkableFlag;
		}

		dtNavMeshCreateParams params{};
		params.verts = polyMesh->verts;
		params.vertCount = polyMesh->nverts;
		params.polys = polyMesh->polys;
		params.polyAreas = polyMesh->areas;
		params.polyFlags = polyMesh->flags;
		params.polyCount = polyMesh->npolys;
		params.nvp = polyMesh->nvp;
		params.detailMeshes = detailMesh->meshes;
		params.detailVerts = detailMesh->verts;
		params.detailVertsCount = detailMesh->nverts;
		params.detailTris = detailMesh->tris;
		params.detailTriCount = detailMesh->ntris;
		params.walkableHeight = settings.AgentHeight;
		params.walkableRadius = settings.AgentRadius;
		params.walkableClimb = settings.AgentMaxClimb;
		rcVcopy(params.bmin, polyMesh->bmin);
		rcVcopy(params.bmax, polyMesh->bmax);
		params.cs = config.cs;
		params.ch = config.ch;
		params.buildBvTree = true;

		unsigned char* data = nullptr;
		int dataSize = 0;
		if (!dtCreateNavMeshData(&params, &data, &dataSize))
		{
			error = "navmesh: cannot create Detour data";
			return false;
		}

		m_NavMesh = dtAllocNavMesh();
		if (!m_NavMesh || dtStatusFailed(m_NavMesh->init(data, dataSize, DT_TILE_FREE_DATA)))
		{
			dtFree(data);
			Release();
			error = "navmesh: cannot initialise the Detour navmesh";
			return false;
		}
		m_Query = dtAllocNavMeshQuery();
		if (!m_Query || dtStatusFailed(m_Query->init(m_NavMesh, MaxQueryNodes)))
		{
			Release();
			error = "navmesh: cannot initialise the navmesh query";
			return false;
		}
		m_PolygonCount = static_cast<uint32_t>(polyMesh->npolys);
		return true;
	}

	std::optional<glm::vec3> NavMesh::ClosestPoint(const glm::vec3& point, const glm::vec3& searchExtents) const
	{
		if (!m_Query)
			return std::nullopt;
		dtQueryFilter filter;
		dtPolyRef ref = 0;
		glm::vec3 nearest(0.0f);
		if (dtStatusFailed(m_Query->findNearestPoly(Ptr(point), Ptr(searchExtents), &filter, &ref, &nearest.x)) || ref == 0)
			return std::nullopt;
		return nearest;
	}

	std::optional<NavPath> NavMesh::FindPath(const glm::vec3& from, const glm::vec3& to) const
	{
		if (!m_Query)
			return std::nullopt;
		dtQueryFilter filter;
		dtPolyRef startRef = 0, endRef = 0;
		glm::vec3 start(0.0f), end(0.0f);
		m_Query->findNearestPoly(Ptr(from), Ptr(DefaultSearchExtents), &filter, &startRef, &start.x);
		m_Query->findNearestPoly(Ptr(to), Ptr(DefaultSearchExtents), &filter, &endRef, &end.x);
		if (startRef == 0 || endRef == 0)
			return std::nullopt;

		std::array<dtPolyRef, MaxPathPolygons> polygons{};
		int polygonCount = 0;
		const dtStatus status = m_Query->findPath(startRef, endRef, &start.x, &end.x, &filter, polygons.data(), &polygonCount, MaxPathPolygons);
		if (dtStatusFailed(status) || polygonCount == 0)
			return std::nullopt;

		NavPath path;
		path.Complete = polygons[static_cast<size_t>(polygonCount - 1)] == endRef && !dtStatusDetail(status, DT_PARTIAL_RESULT);
		glm::vec3 goal = end;
		if (!path.Complete)
			m_Query->closestPointOnPoly(polygons[static_cast<size_t>(polygonCount - 1)], &end.x, &goal.x, nullptr);

		std::array<glm::vec3, MaxPathPolygons> corners{};
		int cornerCount = 0;
		m_Query->findStraightPath(&start.x, &goal.x, polygons.data(), polygonCount, &corners[0].x, nullptr, nullptr, &cornerCount,
			MaxPathPolygons, 0);
		if (cornerCount == 0)
			return std::nullopt;
		path.Points.assign(corners.begin(), corners.begin() + cornerCount);
		for (size_t i = 1; i < path.Points.size(); ++i)
			path.Length += glm::distance(path.Points[i - 1], path.Points[i]);
		return path;
	}

	std::optional<glm::vec3> NavMesh::RandomPointAround(const glm::vec3& center, float radius, uint32_t& randomState) const
	{
		if (!m_Query || radius <= 0.0f)
			return std::nullopt;
		dtQueryFilter filter;
		dtPolyRef centerRef = 0;
		glm::vec3 start(0.0f);
		m_Query->findNearestPoly(Ptr(center), Ptr(DefaultSearchExtents), &filter, &centerRef, &start.x);
		if (centerRef == 0)
			return std::nullopt;

		// Uniform samples in the disc, snapped to the navmesh and kept when inside the radius and
		// reachable from the centre (Detour's own query samples whole polygons, which on large
		// open floors mostly lands outside a small radius).
		const glm::vec3 snapExtents(0.5f, 2.0f, 0.5f);
		std::array<dtPolyRef, MaxPathPolygons> polygons{};
		for (int attempt = 0; attempt < 16; ++attempt)
		{
			const float angle = glm::two_pi<float>() * NextRandom(randomState);
			const float distance = radius * std::sqrt(NextRandom(randomState));
			const glm::vec3 candidate = start + glm::vec3(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance);
			dtPolyRef ref = 0;
			glm::vec3 point(0.0f);
			m_Query->findNearestPoly(Ptr(candidate), Ptr(snapExtents), &filter, &ref, &point.x);
			if (ref == 0 || glm::distance(glm::vec2(point.x, point.z), glm::vec2(start.x, start.z)) > radius)
				continue;
			int count = 0;
			const dtStatus status = m_Query->findPath(centerRef, ref, &start.x, &point.x, &filter, polygons.data(), &count, MaxPathPolygons);
			if (dtStatusSucceed(status) && count > 0 && polygons[static_cast<size_t>(count - 1)] == ref)
				return point;
		}
		return std::nullopt;
	}

	std::vector<glm::vec3> NavMesh::GetDebugEdges() const
	{
		std::vector<glm::vec3> edges;
		if (!m_NavMesh)
			return edges;
		const dtNavMesh& mesh = *m_NavMesh;
		for (int t = 0; t < mesh.getMaxTiles(); ++t)
		{
			const dtMeshTile* tile = mesh.getTile(t);
			if (!tile || !tile->header)
				continue;
			for (int p = 0; p < tile->header->polyCount; ++p)
			{
				const dtPoly& poly = tile->polys[p];
				for (int v = 0; v < poly.vertCount; ++v)
				{
					const float* a = &tile->verts[poly.verts[v] * 3];
					const float* b = &tile->verts[poly.verts[(v + 1) % poly.vertCount] * 3];
					edges.emplace_back(a[0], a[1], a[2]);
					edges.emplace_back(b[0], b[1], b[2]);
				}
			}
		}
		return edges;
	}

}
