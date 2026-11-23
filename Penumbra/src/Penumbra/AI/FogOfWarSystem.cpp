#include "pnpch.h"
#include "Penumbra/AI/FogOfWarSystem.h"

#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/Components.h"
#include "Penumbra/Scene/Scene.h"

#include <glm/gtc/constants.hpp>

namespace Penumbra {

	namespace {

		float SmoothStep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-5f), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// Heights (fractions of a standing character) sampled on characters without silhouette points.
		constexpr float ActorHeights[] = { 0.15f, 0.5f, 0.85f };

	}

	FogOfWarView FogOfWarView::FromComponent(const FogOfWarComponent& component)
	{
		FogOfWarView view;
		view.HalfAngle = std::clamp(component.ViewAngle * 0.5f, 0.0f, 180.0f);
		view.EdgeSoftness = std::max(component.EdgeSoftness, 0.0f);
		view.ViewDistance = std::max(component.ViewDistance, 0.0f);
		view.AwarenessRadius = std::max(component.AwarenessRadius, 0.0f);
		view.SeeOverHeight = std::max(component.SeeOverHeight, 0.0f);
		view.Brightness = std::clamp(component.Brightness, 0.0f, 1.0f);
		view.Saturation = std::clamp(component.Saturation, 0.0f, 1.0f);
		return view;
	}

	// Keep in step with FogCoverage in shaders/FogOfWar.hlsli.
	float FogOfWarView::Coverage(const glm::vec3& point) const
	{
		const glm::vec3 offset = point - Eye;
		const float eyeDistance = glm::length(glm::vec2(offset.x, offset.z));
		const glm::vec3 fromCone = point - ConeOrigin.value_or(Eye);
		const glm::vec2 flat(fromCone.x, fromCone.z);
		const float flatDistance = glm::length(flat);

		const float circle = 1.0f - SmoothStep(AwarenessRadius * 0.7f, AwarenessRadius, eyeDistance);
		float cone = 1.0f;
		if (flatDistance > 1e-4f)
		{
			const float cosAngle = std::clamp(glm::dot(flat / flatDistance, glm::vec2(Forward.x, Forward.z)), -1.0f, 1.0f);
			const float angle = glm::degrees(std::acos(cosAngle));
			cone = 1.0f - SmoothStep(HalfAngle - EdgeSoftness * 0.5f, HalfAngle + EdgeSoftness * 0.5f, angle);
		}
		const float range = 1.0f - SmoothStep(ViewDistance * 0.85f, ViewDistance, glm::length(offset));
		return std::max(circle, cone) * range;
	}

	FogOfWarSystem::FogOfWarSystem(Scene& scene)
		: m_Scene(scene)
	{
	}

	std::optional<FogOfWarView> FogOfWarSystem::ComputeView(float dt)
	{
		if (m_ForcedOff)
			return std::nullopt;
		for (auto [handle, fog] : m_Scene.GetRegistry().view<FogOfWarComponent>().each())
		{
			fog.RuntimeActive = false;
			if (!fog.Enabled)
				continue;
			Entity viewer(handle, &m_Scene);
			FogOfWarView view = FogOfWarView::FromComponent(fog);
			view.Viewer = viewer.GetUUID();

			// A crouching character's eye sinks with his capsule, easing like the player's camera (the capsule changes in a
			// frame, the crouch takes a moment: the view darkens behind a crate as he gets down, not before).
			float eyeHeight = fog.EyeHeight;
			if (const auto* controller = viewer.TryGetComponent<CharacterControllerComponent>())
				eyeHeight = std::min(eyeHeight, std::max(controller->Height - 0.15f, 0.2f));
			if (fog.RuntimeEyeHeight >= 0.0f && fog.EyeSpeed > 0.0f)
				eyeHeight = fog.RuntimeEyeHeight + (eyeHeight - fog.RuntimeEyeHeight) * std::min(std::max(dt, 0.0f) * fog.EyeSpeed, 1.0f);
			fog.RuntimeEyeHeight = eyeHeight;
			view.Eye = m_Scene.GetWorldPosition(viewer) + glm::vec3(0.0f, eyeHeight, 0.0f);

			// He faces where the camera looks (the player aims the camera); without one, where his body faces.
			glm::vec3 forward(0.0f);
			if (Entity camera = m_Scene.GetPrimaryCameraEntity())
			{
				forward = Math::Forward(m_Scene.GetWorldRotation(camera));
				// The whole frame is looked at: the cone starts at the camera and is as wide as the picture, its fading
				// edge just outside it.
				if (fog.ConeFromCamera && camera.HasComponent<CameraComponent>())
				{
					const float aspect = static_cast<float>(std::max(m_Scene.GetViewportWidth(), 1u)) /
										 static_cast<float>(std::max(m_Scene.GetViewportHeight(), 1u));
					const float vertical = glm::radians(std::clamp(camera.GetComponent<CameraComponent>().VerticalFOV, 1.0f, 179.0f));
					const float frameHalf = glm::degrees(std::atan(std::tan(vertical * 0.5f) * aspect));
					view.HalfAngle = std::min(frameHalf + view.EdgeSoftness * 0.5f, 180.0f);
					view.ConeOrigin = m_Scene.GetWorldPosition(camera);
				}
			}
			forward.y = 0.0f;
			if (glm::length(forward) < 1e-3f)
			{
				forward = Math::Forward(m_Scene.GetWorldRotation(viewer));
				forward.y = 0.0f;
			}
			view.Forward = glm::length(forward) < 1e-3f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::normalize(forward);
			// Leaning (Q/E) moves the eye sideways, square to where he looks.
			view.Eye += glm::cross(view.Forward, glm::vec3(0.0f, 1.0f, 0.0f)) * fog.Lean;

			fog.RuntimeActive = true;
			fog.RuntimeEye = view.Eye;
			fog.RuntimeForward = view.Forward;
			return view; // one viewer: the first enabled one
		}
		return std::nullopt;
	}

	bool FogOfWarSystem::IsActor(Entity entity) const
	{
		return entity.HasComponent<PerceptionComponent>() || entity.HasComponent<CharacterControllerComponent>() ||
			   entity.HasComponent<RagdollComponent>();
	}

	bool FogOfWarSystem::IsLineClear(const glm::vec3& eye, const glm::vec3& point, UUID target)
	{
		PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		if (!physics)
			return true;
		const glm::vec3 offset = point - eye;
		const float distance = glm::length(offset);
		if (distance < 1e-3f)
			return true;
		const UUID viewer = m_View ? m_View->Viewer : UUID(UUID::Invalid);
		// Walls, floors, doors and furniture block the view; characters and loose objects do not
		// (the renderer's eye map is built from the same kinds of things).
		auto filter = [&](UUID id) {
			std::optional<Entity> entity = m_Scene.TryGetEntityByUUID(id);
			if (!entity)
				return false;
			if (const RigidBodyComponent* body = entity->TryGetComponent<RigidBodyComponent>();
				body && (body->TransparentToSight || body->IsTrigger || body->Type == RigidBodyType::Dynamic))
				return false;
			// Nothing a character is or carries (his own collider, a shield on his arm).
			for (Entity current = *entity; current; current = current.GetParent())
			{
				if (current.GetUUID() == viewer || current.GetUUID() == target || IsActor(current))
					return false;
			}
			return true;
		};
		const std::optional<RaycastHit> hit = physics->Raycast(eye, offset / distance, distance, filter);
		return !hit || hit->Distance >= distance - 0.05f;
	}

	bool FogOfWarSystem::CanSee(const glm::vec3& point)
	{
		if (!m_View)
			return true;
		if (m_View->Coverage(point) <= 0.5f)
			return false;
		if (IsLineClear(m_View->Eye, point, UUID::Invalid))
			return true;
		// Like the renderer: the space above the point, up to the eye, seen over something low counts too.
		const float lifted = std::min(point.y + m_View->SeeOverHeight, m_View->Eye.y);
		return lifted > point.y + 0.05f && IsLineClear(m_View->Eye, glm::vec3(point.x, lifted, point.z), UUID::Invalid);
	}

	bool FogOfWarSystem::CanSeeEntity(Entity entity)
	{
		if (!m_View || !entity)
			return true;
		if (entity.GetUUID() == m_View->Viewer)
			return true;
		if (std::optional<Entity> viewer = m_Scene.TryGetEntityByUUID(m_View->Viewer); viewer && m_Scene.IsAncestor(*viewer, entity))
			return true; // what he carries

		const glm::mat4 world = m_Scene.GetWorldTransform(entity);
		std::vector<glm::vec3> points;
		if (const auto* target = entity.TryGetComponent<PerceptionTargetComponent>(); target && !target->SilhouettePoints.empty())
		{
			for (const glm::vec3& local : target->SilhouettePoints)
				points.push_back(glm::vec3(world * glm::vec4(local, 1.0f)));
		}
		else
		{
			float height = 1.8f;
			if (const auto* controller = entity.TryGetComponent<CharacterControllerComponent>())
				height = controller->Height;
			const glm::vec3 origin(world[3]);
			for (float fraction : ActorHeights)
				points.push_back(origin + glm::vec3(0.0f, height * fraction, 0.0f));
		}
		for (const glm::vec3& point : points)
		{
			if (m_View->Coverage(point) > 0.5f && IsLineClear(m_View->Eye, point, entity.GetUUID()))
				return true;
		}
		return false;
	}

	void FogOfWarSystem::Update(Timestep ts)
	{
		m_View = ComputeView(ts.GetSeconds());
		m_Hidden.clear();
		if (!m_View)
			return;
		std::optional<Entity> viewer = m_Scene.TryGetEntityByUUID(m_View->Viewer);
		FogOfWarComponent* fog = viewer ? viewer->TryGetComponent<FogOfWarComponent>() : nullptr;
		if (!fog || !fog->HideActors)
		{
			if (fog)
				fog->RuntimeHiddenActors = 0;
			return;
		}

		const float now = m_Scene.GetTime();
		std::unordered_set<UUID> checked;
		auto consider = [&](entt::entity handle) {
			Entity entity(handle, &m_Scene);
			const UUID id = entity.GetUUID();
			if (!checked.insert(id).second || id == m_View->Viewer || m_Scene.IsAncestor(*viewer, entity))
				return;
			if (CanSeeEntity(entity))
				m_LastSeen[id] = now;
			const auto seen = m_LastSeen.find(id);
			if (seen == m_LastSeen.end() || now - seen->second > fog->MemoryTime)
				m_Hidden.insert(id);
		};
		auto& registry = m_Scene.GetRegistry();
		for (entt::entity handle : registry.view<PerceptionComponent>())
			consider(handle);
		for (entt::entity handle : registry.view<CharacterControllerComponent>())
			consider(handle);
		for (entt::entity handle : registry.view<RagdollComponent>())
			consider(handle);
		std::erase_if(m_LastSeen, [&](const auto& entry) { return !checked.contains(entry.first); });
		fog->RuntimeHiddenActors = static_cast<uint32_t>(m_Hidden.size());
	}

	bool FogOfWarSystem::IsHidden(Entity entity)
	{
		if (m_Hidden.empty())
			return false;
		for (Entity current = entity; current; current = current.GetParent())
		{
			if (m_Hidden.contains(current.GetUUID()))
				return true;
		}
		return false;
	}

}
