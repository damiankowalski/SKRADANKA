#include "pnpch.h"
#include "Penumbra/Navigation/NavigationSystem.h"

#include "Penumbra/Physics/PhysicsWorld.h"

namespace Penumbra {

	namespace {

		float HorizontalDistance(const glm::vec3& a, const glm::vec3& b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

		bool IsStaticSolidCollider(Entity entity)
		{
			const bool hasCollider = entity.HasComponent<BoxColliderComponent>() || entity.HasComponent<SphereColliderComponent>() ||
				entity.HasComponent<CapsuleColliderComponent>() || entity.HasComponent<MeshColliderComponent>();
			if (!hasCollider || entity.HasComponent<CharacterControllerComponent>())
				return false;
			const RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>();
			return !body || (body->Type == RigidBodyType::Static && !body->IsTrigger);
		}

	}

	NavigationSystem::NavigationSystem(Scene& scene)
		: m_Scene(scene)
	{
	}

	NavigationSystem::~NavigationSystem() = default;

	bool NavigationSystem::Bake(std::string& error)
	{
		m_BakeAttempted = true;
		m_Dirty = false;
		m_NavMesh.reset();

		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		if (!physics)
		{
			error = "navmesh: the scene is not running";
			return false;
		}

		NavMeshBuildSettings settings;
		NavMeshComponent* component = nullptr;
		auto view = m_Scene.GetRegistry().view<NavMeshComponent>();
		if (const entt::entity first = view.front(); first != entt::null)
		{
			component = &view.get<NavMeshComponent>(first);
			settings = { component->CellSize, component->CellHeight, component->AgentRadius, component->AgentHeight, component->AgentMaxClimb,
				component->AgentMaxSlope };
		}

		std::vector<glm::vec3> vertices;
		std::vector<uint32_t> indices;
		physics->CollectStaticTriangles(vertices, indices);

		const auto start = std::chrono::steady_clock::now();
		Scope<NavMesh> navMesh = CreateScope<NavMesh>();
		if (!navMesh->Build(vertices, indices, settings, error))
		{
			PN_CORE_WARN("{}", error);
			return false;
		}
		const float milliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		if (component)
		{
			component->RuntimePolygons = navMesh->GetPolygonCount();
			component->RuntimeBuildMilliseconds = milliseconds;
		}
		PN_CORE_TRACE("Navmesh: {} polygons from {} triangles in {:.1f} ms", navMesh->GetPolygonCount(), indices.size() / 3, milliseconds);
		m_NavMesh = std::move(navMesh);

		// Paths computed on the old navmesh may cross what changed: refresh them now.
		for (auto& [uuid, state] : m_Agents)
			state.ReplanTimer = 0.0f;
		return true;
	}

	const NavMesh* NavigationSystem::GetNavMesh()
	{
		if (!m_BakeAttempted)
		{
			std::string error;
			Bake(error);
		}
		return m_NavMesh.get();
	}

	void NavigationSystem::OnEntityDestroyed(Entity entity)
	{
		m_Agents.erase(entity.GetUUID());
		if (m_NavMesh && IsStaticSolidCollider(entity))
			m_Dirty = true;
	}

	std::optional<NavPath> NavigationSystem::FindPath(const glm::vec3& from, const glm::vec3& to)
	{
		const NavMesh* navMesh = GetNavMesh();
		return navMesh ? navMesh->FindPath(from, to) : std::nullopt;
	}

	std::optional<glm::vec3> NavigationSystem::ClosestPoint(const glm::vec3& point, float searchRadius)
	{
		const NavMesh* navMesh = GetNavMesh();
		if (!navMesh)
			return std::nullopt;
		const float radius = std::max(searchRadius, 0.05f);
		return navMesh->ClosestPoint(point, glm::vec3(radius, std::max(radius, 3.0f), radius));
	}

	std::optional<glm::vec3> NavigationSystem::RandomPoint(const glm::vec3& center, float radius)
	{
		const NavMesh* navMesh = GetNavMesh();
		return navMesh ? navMesh->RandomPointAround(center, radius, m_RandomState) : std::nullopt;
	}

	bool NavigationSystem::MoveTo(Entity agent, const glm::vec3& destination, std::optional<float> speed)
	{
		auto& component = agent.HasComponent<NavAgentComponent>() ? agent.GetComponent<NavAgentComponent>() : agent.AddComponent<NavAgentComponent>();
		AgentState& state = m_Agents[agent.GetUUID()];
		state.Speed = std::max(speed.value_or(component.Speed), 0.0f);
		state.StuckTimer = 0.0f;
		component.RuntimeDestination = destination;
		component.RuntimeStatus = NavStatus::Moving;
		return Replan(agent, component, state);
	}

	void NavigationSystem::Stop(Entity agent)
	{
		m_Agents.erase(agent.GetUUID());
		if (NavAgentComponent* component = agent.TryGetComponent<NavAgentComponent>())
		{
			if (component->RuntimeStatus == NavStatus::Moving)
				component->RuntimeStatus = NavStatus::Idle;
			component->RuntimeRemainingDistance = 0.0f;
		}
		if (CharacterControllerComponent* controller = agent.TryGetComponent<CharacterControllerComponent>())
			controller->RuntimeDesiredVelocity = glm::vec3(0.0f);
	}

	const std::vector<glm::vec3>* NavigationSystem::GetAgentPath(UUID agent) const
	{
		const auto it = m_Agents.find(agent);
		return it != m_Agents.end() ? &it->second.Path : nullptr;
	}

	bool NavigationSystem::Replan(Entity agent, NavAgentComponent& component, AgentState& state)
	{
		state.ReplanTimer = ReplanInterval;
		std::optional<NavPath> path = FindPath(m_Scene.GetWorldPosition(agent), component.RuntimeDestination);
		if (!path || path->Points.empty())
		{
			component.RuntimeStatus = NavStatus::Failed;
			component.RuntimePartialPath = false;
			state.Path.clear();
			if (CharacterControllerComponent* controller = agent.TryGetComponent<CharacterControllerComponent>())
				controller->RuntimeDesiredVelocity = glm::vec3(0.0f);
			return false;
		}
		state.Path = std::move(path->Points);
		state.Next = std::min<size_t>(1, state.Path.size() - 1);
		component.RuntimePartialPath = !path->Complete;
		component.RuntimeRemainingDistance = path->Length;
		return true;
	}

	void NavigationSystem::FaceDirection(Entity agent, const glm::vec3& direction, float maxDegrees)
	{
		const glm::vec3 flat(direction.x, 0.0f, direction.z);
		if (glm::length(flat) < Math::Epsilon)
			return;
		const glm::quat current = m_Scene.GetWorldRotation(agent);
		const glm::quat target = Math::LookRotation(flat);
		const float cosine = std::clamp(std::abs(glm::dot(current, target)), 0.0f, 1.0f);
		const float angle = glm::degrees(2.0f * std::acos(cosine));
		glm::quat world = angle <= maxDegrees || angle < 1e-3f ? target : glm::slerp(current, target, maxDegrees / angle);
		if (Entity parent = agent.GetParent())
			world = glm::inverse(m_Scene.GetWorldRotation(parent)) * world;
		agent.GetComponent<TransformComponent>().Rotation = glm::normalize(world);
	}

	void NavigationSystem::SteerAgent(Entity agent, NavAgentComponent& component, CharacterControllerComponent& controller, AgentState& state, float dt)
	{
		state.ReplanTimer -= dt;
		if ((state.ReplanTimer <= 0.0f || state.Path.empty()) && !Replan(agent, component, state))
			return;

		const glm::vec3 position = m_Scene.GetWorldPosition(agent);
		const size_t last = state.Path.size() - 1;
		while (state.Next < last && HorizontalDistance(position, state.Path[state.Next]) < CornerRadius)
			state.Next++;

		const glm::vec3& corner = state.Path[state.Next];
		const float toCorner = HorizontalDistance(position, corner);
		float remaining = toCorner;
		for (size_t i = state.Next; i < last; ++i)
			remaining += HorizontalDistance(state.Path[i], state.Path[i + 1]);
		component.RuntimeRemainingDistance = remaining;

		if (state.Next == last && toCorner <= std::max(component.StoppingDistance, 0.05f))
		{
			component.RuntimeStatus = NavStatus::Arrived;
			controller.RuntimeDesiredVelocity = glm::vec3(0.0f);
			m_Agents.erase(agent.GetUUID());
			return;
		}

		glm::vec3 direction(corner.x - position.x, 0.0f, corner.z - position.z);
		direction = toCorner > Math::Epsilon ? direction / toCorner : glm::vec3(0.0f);
		// Ease into the final stop so agents do not overshoot and turn around.
		const float speed = state.Speed * (state.Next == last ? std::clamp(toCorner / 0.6f, 0.35f, 1.0f) : 1.0f);
		controller.RuntimeDesiredVelocity = direction * speed;
		if (component.FaceMovement)
			FaceDirection(agent, direction, component.TurnSpeed * dt);

		const float actualSpeed = glm::length(glm::vec2(controller.RuntimeVelocity.x, controller.RuntimeVelocity.z));
		state.StuckTimer = actualSpeed < speed * 0.25f ? state.StuckTimer + dt : 0.0f;
		if (state.StuckTimer > StuckTime)
		{
			component.RuntimeStatus = NavStatus::Stuck;
			controller.RuntimeDesiredVelocity = glm::vec3(0.0f);
			m_Agents.erase(agent.GetUUID());
		}
	}

	void NavigationSystem::Update(Timestep ts)
	{
		m_RebuildTimer = std::max(m_RebuildTimer - ts.GetSeconds(), 0.0f);
		if (m_Dirty && m_RebuildTimer <= 0.0f)
		{
			std::string error;
			Bake(error);
			m_RebuildTimer = RebuildCooldown;
		}

		std::vector<std::pair<Entity, UUID>> agents;
		for (auto [handle, component] : m_Scene.GetRegistry().view<NavAgentComponent>().each())
		{
			if (component.RuntimeStatus == NavStatus::Moving)
			{
				Entity entity(handle, &m_Scene);
				agents.emplace_back(entity, entity.GetUUID());
			}
		}
		// Stable order (the registry order can change as entities come and go).
		std::sort(agents.begin(), agents.end(), [](const auto& a, const auto& b) { return static_cast<uint64_t>(a.second) < static_cast<uint64_t>(b.second); });

		for (auto& [entity, uuid] : agents)
		{
			auto& component = entity.GetComponent<NavAgentComponent>();
			CharacterControllerComponent* controller = entity.TryGetComponent<CharacterControllerComponent>();
			const auto it = m_Agents.find(uuid);
			if (!controller || it == m_Agents.end())
			{
				// Moving without a plan (e.g. status set by hand or loaded from a file): stand still.
				component.RuntimeStatus = controller ? NavStatus::Idle : NavStatus::Failed;
				continue;
			}
			SteerAgent(entity, component, *controller, it->second, ts.GetSeconds());
		}
	}

}
