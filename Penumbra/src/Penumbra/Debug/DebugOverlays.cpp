#include "pnpch.h"
#include "Penumbra/Debug/DebugOverlays.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Math/Math.h"
#include "Penumbra/Navigation/NavMesh.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Renderer/DebugDraw.h"
#include "Penumbra/Scene/Scene.h"

namespace Penumbra::DebugOverlays {

	namespace {
		void CollectHierarchy(Entity entity, std::vector<UUID>& out)
		{
			out.push_back(entity.GetUUID());
			for (Entity child : entity.GetChildren())
				CollectHierarchy(child, out);
		}

		// What stops a debug sight ray: solid bodies, as for the guards' eyes, but not characters
		// (NPCs and the thief stand in the fan, they do not end it).
		bool StopsRay(Scene& scene, UUID id, const std::vector<UUID>& ignore)
		{
			if (std::find(ignore.begin(), ignore.end(), id) != ignore.end())
				return false;
			std::optional<Entity> entity = scene.TryGetEntityByUUID(id);
			if (!entity)
				return false;
			if (entity->HasComponent<CharacterControllerComponent>() || entity->HasComponent<PerceptionTargetComponent>())
				return false;
			if (const RigidBodyComponent* body = entity->TryGetComponent<RigidBodyComponent>())
				return !body->TransparentToSight && !body->IsTrigger;
			return true;
		}

		glm::vec3 Eye(Scene& scene, Entity observer, const PerceptionComponent& perception)
		{
			return scene.GetWorldPosition(observer) + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
		}

		glm::vec3 HorizontalForward(Scene& scene, Entity observer)
		{
			glm::vec3 forward = Math::Forward(scene.GetWorldRotation(observer));
			forward.y = 0.0f;
			const float length = glm::length(forward);
			return length > Math::Epsilon ? forward / length : glm::vec3(0.0f, 0.0f, -1.0f);
		}
	}

	glm::vec4 StateColor(AwarenessState state)
	{
		switch (state)
		{
			case AwarenessState::Unaware:       return { 0.3f, 0.9f, 0.4f, 1.0f };
			case AwarenessState::Suspicious:    return { 1.0f, 0.9f, 0.2f, 1.0f };
			case AwarenessState::Investigating: return { 1.0f, 0.72f, 0.15f, 1.0f };
			case AwarenessState::Searching:     return { 1.0f, 0.55f, 0.1f, 1.0f };
			case AwarenessState::Alarmed:       return { 1.0f, 0.15f, 0.1f, 1.0f };
		}
		return glm::vec4(1.0f);
	}

	std::vector<VisionFanRay> ComputeVisionFan(Scene& scene, Entity observer, float stepDegrees)
	{
		std::vector<VisionFanRay> rays;
		const PerceptionComponent* perception = observer.TryGetComponent<PerceptionComponent>();
		PhysicsWorld* physics = scene.GetPhysicsWorld();
		if (!perception)
			return rays;

		std::vector<UUID> ignore;
		CollectHierarchy(observer, ignore);
		auto filter = [&](UUID id) { return StopsRay(scene, id, ignore); };

		const glm::vec3 eye = Eye(scene, observer, *perception);
		const glm::vec3 forward = HorizontalForward(scene, observer);
		const float half = std::clamp(perception->FieldOfView, 1.0f, 359.0f) * 0.5f;
		const float focusHalf = std::min(std::max(perception->FocusFieldOfView, 0.0f) * 0.5f, half);
		const int steps = std::max(2, static_cast<int>(std::ceil(2.0f * half / std::max(stepDegrees, 0.5f))));
		rays.reserve(static_cast<size_t>(steps) + 1);
		for (int i = 0; i <= steps; ++i)
		{
			const float angle = -half + 2.0f * half * static_cast<float>(i) / static_cast<float>(steps);
			const glm::vec3 direction = glm::angleAxis(glm::radians(angle), glm::vec3(0.0f, 1.0f, 0.0f)) * forward;
			VisionFanRay ray;
			ray.Focus = std::abs(angle) <= focusHalf + 1e-3f;
			ray.End = eye + direction * perception->ViewDistance;
			if (std::optional<RaycastHit> hit = physics ? physics->Raycast(eye, direction, perception->ViewDistance, filter) : std::nullopt)
			{
				ray.End = hit->Point;
				ray.Blocked = true;
			}
			rays.push_back(ray);
		}
		return rays;
	}

	std::vector<SightLine> ComputeSightLines(Scene& scene, Entity observer, Entity target)
	{
		std::vector<SightLine> lines;
		const PerceptionComponent* perception = observer.TryGetComponent<PerceptionComponent>();
		const PerceptionTargetComponent* targetComponent = target.TryGetComponent<PerceptionTargetComponent>();
		PhysicsWorld* physics = scene.GetPhysicsWorld();
		if (!perception || !targetComponent || !physics)
			return lines;

		std::vector<UUID> ignore;
		CollectHierarchy(observer, ignore);
		CollectHierarchy(target, ignore);
		auto filter = [&](UUID id) { return StopsRay(scene, id, ignore); };

		const glm::vec3 eye = Eye(scene, observer, *perception);
		const glm::vec3 forward = glm::normalize(Math::Forward(scene.GetWorldRotation(observer)));
		const float half = glm::radians(std::clamp(perception->FieldOfView, 1.0f, 359.0f) * 0.5f);
		const float focusHalf = std::min(glm::radians(std::max(perception->FocusFieldOfView, 0.0f) * 0.5f), half);
		const glm::mat4 frame = Math::ComposeTransform(scene.GetWorldPosition(target), scene.GetWorldRotation(target), glm::vec3(1.0f));
		for (const glm::vec3& local : targetComponent->SilhouettePoints)
		{
			const glm::vec3 point = glm::vec3(frame * glm::vec4(local, 1.0f));
			glm::vec3 toPoint = point - eye;
			const float distance = glm::length(toPoint);
			if (distance < Math::Epsilon || distance > perception->ViewDistance)
			{
				lines.push_back(SightLine::OutOfView);
				continue;
			}
			toPoint /= distance;
			const float angle = std::acos(std::clamp(glm::dot(forward, toPoint), -1.0f, 1.0f));
			if (angle > half)
				lines.push_back(SightLine::OutOfView);
			else if (physics->Raycast(eye, toPoint, distance, filter))
				lines.push_back(SightLine::Occluded);
			else
				lines.push_back(angle <= focusHalf ? SightLine::Focus : SightLine::Peripheral);
		}
		return lines;
	}

	void DrawVision(Scene& scene, DebugDraw& debug, UUID onlyObserver)
	{
		std::vector<Entity> intruders;
		for (auto [handle, target] : scene.GetRegistry().view<PerceptionTargetComponent>().each())
		{
			if (target.Kind == TargetKind::Intruder)
				intruders.emplace_back(handle, &scene);
		}

		for (auto [handle, perception] : scene.GetRegistry().view<PerceptionComponent>().each())
		{
			Entity observer(handle, &scene);
			if (onlyObserver != UUID::Invalid && observer.GetUUID() != onlyObserver)
				continue;
			if (!PerceptionSystem::IsAwake(observer))
				continue;
			const glm::vec3 eye = Eye(scene, observer, perception);
			const glm::vec4 color = StateColor(perception.RuntimeState);
			const glm::vec4 dim = color * glm::vec4(1.0f, 1.0f, 1.0f, 0.6f);

			// The fan: its outline at eye height, a spoke every other ray.
			const std::vector<VisionFanRay> fan = ComputeVisionFan(scene, observer);
			if (!fan.empty())
			{
				debug.Line(eye, fan.front().End, dim);
				debug.Line(eye, fan.back().End, dim);
				for (size_t i = 0; i < fan.size(); ++i)
				{
					if (i + 1 < fan.size())
						debug.Line(fan[i].End, fan[i + 1].End, fan[i].Focus && fan[i + 1].Focus ? color : dim);
					// The focus cone's edges, and a spoke every 10 degrees.
					const bool focusEdge = i > 0 && fan[i].Focus != fan[i - 1].Focus;
					if (focusEdge || i % 2 == 0)
						debug.Line(eye, fan[i].End, focusEdge ? color : dim * glm::vec4(1.0f, 1.0f, 1.0f, 0.75f));
				}
			}

			// Suspicion meter above the head.
			const glm::vec3 barStart = eye + glm::vec3(-0.5f, 0.5f, 0.0f);
			debug.Line(barStart, barStart + glm::vec3(1.0f, 0.0f, 0.0f), { 0.2f, 0.2f, 0.2f, 1.0f });
			debug.Line(barStart + glm::vec3(0.0f, 0.03f, 0.0f), barStart + glm::vec3(perception.RuntimeSuspicion, 0.03f, 0.0f), color);
			if (perception.RuntimeHasStimulus)
			{
				debug.Line(eye, perception.RuntimeLastStimulusPosition, { color.r, color.g, color.b, 0.6f });
				debug.Sphere(perception.RuntimeLastStimulusPosition, 0.2f, color);
			}

			// The thief's silhouette points this guard has a line of sight to.
			for (Entity intruder : intruders)
			{
				const std::vector<SightLine> lines = ComputeSightLines(scene, observer, intruder);
				const PerceptionTargetComponent& target = intruder.GetComponent<PerceptionTargetComponent>();
				const glm::mat4 frame =
					Math::ComposeTransform(scene.GetWorldPosition(intruder), scene.GetWorldRotation(intruder), glm::vec3(1.0f));
				for (size_t i = 0; i < lines.size() && i < target.SilhouettePoints.size(); ++i)
				{
					if (lines[i] == SightLine::OutOfView || lines[i] == SightLine::Occluded)
						continue;
					const glm::vec3 point = glm::vec3(frame * glm::vec4(target.SilhouettePoints[i], 1.0f));
					debug.Line(eye, point, lines[i] == SightLine::Focus ? glm::vec4(1.0f, 0.3f, 0.9f, 0.9f) : glm::vec4(0.7f, 0.4f, 1.0f, 0.5f));
				}
			}
		}
	}

	void DrawNoises(DebugDraw& debug, const std::vector<DebugNoise>& noises, float now, float lifetime)
	{
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		for (const DebugNoise& noise : noises)
		{
			const float age = now - noise.Time;
			if (age < 0.0f || age > lifetime)
				continue;
			const float fade = 1.0f - age / lifetime;
			const glm::vec3 center = noise.Position + glm::vec3(0.0f, 0.05f, 0.0f);
			// Outer: heard at all (level 0). Inner: heard at level 4, enough for a guard to walk over.
			const float outer = PerceptionModel::NoiseRange(noise.Loudness);
			const float inner = PerceptionModel::NoiseRange(noise.Loudness - 4.0f);
			// A ring grows out to its radius in the first fifth of a second, like a sound wave.
			const float grow = std::min(age / 0.2f, 1.0f);
			debug.Circle(center, up, std::max(outer * grow, 0.05f), { 0.4f, 0.8f, 1.0f, 0.8f * fade }, 64);
			if (inner > 0.0f)
				debug.Circle(center, up, std::max(inner * grow, 0.05f), { 1.0f, 0.75f, 0.2f, 0.9f * fade }, 48);
			debug.Sphere(noise.Position, 0.12f, { 0.4f, 0.8f, 1.0f, fade });
		}
	}

	void DrawNavigation(Scene& scene, DebugDraw& debug)
	{
		NavigationSystem* navigation = scene.GetNavigationSystem();
		if (!navigation)
			return; // the navmesh exists while the game runs (it is baked from the physics world)
		if (const NavMesh* navMesh = navigation->GetNavMesh())
		{
			const std::vector<glm::vec3> edges = navMesh->GetDebugEdges();
			const glm::vec3 lift(0.0f, 0.05f, 0.0f);
			for (size_t i = 0; i + 1 < edges.size(); i += 2)
				debug.Line(edges[i] + lift, edges[i + 1] + lift, { 0.2f, 0.6f, 1.0f, 0.5f });
		}
		for (auto [handle, agent] : scene.GetRegistry().view<NavAgentComponent>().each())
		{
			Entity entity(handle, &scene);
			const std::vector<glm::vec3>* path = navigation->GetAgentPath(entity.GetUUID());
			if (!path || path->empty())
				continue;
			glm::vec3 previous = scene.GetWorldPosition(entity) + glm::vec3(0.0f, 0.1f, 0.0f);
			for (const glm::vec3& corner : *path)
			{
				debug.Line(previous, corner + glm::vec3(0.0f, 0.1f, 0.0f), { 1.0f, 0.9f, 0.2f, 1.0f });
				previous = corner + glm::vec3(0.0f, 0.1f, 0.0f);
			}
			debug.Sphere(agent.RuntimeDestination, 0.15f, { 1.0f, 0.9f, 0.2f, 1.0f });
		}
	}

	void DrawColliders(Scene& scene, DebugDraw& debug)
	{
		const glm::vec4 color(0.3f, 1.0f, 0.5f, 0.8f);
		for (auto [handle, box] : scene.GetRegistry().view<BoxColliderComponent>().each())
		{
			Entity entity(handle, &scene);
			glm::vec3 translation, scale;
			glm::quat rotation;
			Math::DecomposeTransform(scene.GetWorldTransform(entity), translation, rotation, scale);
			debug.Box(translation + rotation * (box.Offset * scale), box.HalfExtents * glm::abs(scale), rotation, color);
		}
		for (auto [handle, sphere] : scene.GetRegistry().view<SphereColliderComponent>().each())
		{
			Entity entity(handle, &scene);
			const glm::mat4 world = scene.GetWorldTransform(entity);
			const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
			debug.Sphere(glm::vec3(world * glm::vec4(sphere.Offset, 1.0f)), sphere.Radius * std::max({ scale.x, scale.y, scale.z }), color);
		}
	}

	void DrawSmoke(Scene& scene, DebugDraw& debug)
	{
		for (auto [handle, smoke] : scene.GetRegistry().view<SmokeVolumeComponent>().each())
			debug.Sphere(scene.GetWorldPosition(Entity(handle, &scene)), smoke.Radius, { 0.7f, 0.7f, 0.7f, 0.3f });
	}

}
