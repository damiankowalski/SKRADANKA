#include "pnpch.h"
#include "Penumbra/AI/PerceptionSystem.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/Physics/PhysicsWorld.h"

namespace Penumbra {

	namespace {

		constexpr float NeutralAlbedo = 0.5f;
		constexpr float BackgroundRangeMultiplier = 3.0f;

		void CollectHierarchy(Entity entity, std::vector<UUID>& out)
		{
			out.push_back(entity.GetUUID());
			for (Entity child : entity.GetChildren())
				CollectHierarchy(child, out);
		}

		bool Contains(const std::vector<UUID>& list, UUID value) { return std::find(list.begin(), list.end(), value) != list.end(); }

		// Sight-line probe directions relative to the view direction (yaw, pitch in degrees),
		// used to estimate the average brightness an observer is looking at.
		constexpr std::array<glm::vec2, 5> s_ViewProbes = { glm::vec2(0.0f, 0.0f), glm::vec2(-20.0f, 0.0f), glm::vec2(20.0f, 0.0f),
			glm::vec2(0.0f, -12.0f), glm::vec2(0.0f, 10.0f) };

	}

	PerceptionSystem::PerceptionSystem(Scene& scene)
		: m_Scene(scene), m_Provider(CreateScope<AnalyticLuminanceProvider>(scene)), m_VisionTimer(VisionInterval)
	{
	}

	void PerceptionSystem::EmitNoise(const glm::vec3& position, float radius, UUID source)
	{
		if (radius > 0.0f)
			m_PendingNoises.push_back({ position, radius, source });
	}

	bool PerceptionSystem::IsSightBlocker(UUID entityID, const std::vector<UUID>& ignore)
	{
		if (Contains(ignore, entityID))
			return false;
		std::optional<Entity> entity = m_Scene.TryGetEntityByUUID(entityID);
		if (!entity)
			return false;
		if (const RigidBodyComponent* body = entity->TryGetComponent<RigidBodyComponent>())
			return !body->TransparentToSight && !body->IsTrigger;
		return true;
	}

	float PerceptionSystem::OpticalDepth(const glm::vec3& from, const glm::vec3& to)
	{
		const glm::vec3 delta = to - from;
		const float length = glm::length(delta);
		if (length < Math::Epsilon)
			return 0.0f;
		const glm::vec3 direction = delta / length;

		float depth = 0.0f;
		if (Entity environment = m_Scene.GetEnvironmentEntity())
			depth += environment.GetComponent<EnvironmentComponent>().FogDensity * length;

		for (auto [handle, smoke] : m_Scene.GetRegistry().view<SmokeVolumeComponent>().each())
		{
			const glm::vec3 center = m_Scene.GetWorldPosition(Entity(handle, &m_Scene));
			// Smoke thins out over the last third of its lifetime.
			float density = smoke.Density;
			if (smoke.Lifetime > 0.0f)
				density *= std::clamp((smoke.Lifetime - smoke.RuntimeAge) / (smoke.Lifetime / 3.0f), 0.0f, 1.0f);
			depth += density * Math::SegmentSphereOverlap(from, direction, length, center, smoke.Radius);
		}
		return depth;
	}

	float PerceptionSystem::SampleLuminance(const glm::vec3& point)
	{
		return PerceptionModel::SurfaceLuminance(NeutralAlbedo, m_Provider->Illuminance(point, glm::vec3(0.0f), {}), 0.0f);
	}

	VisibilityResult PerceptionSystem::ComputeVisibility(Entity observer, Entity target)
	{
		VisibilityResult result;
		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		if (!physics || !observer.HasComponent<PerceptionComponent>() || !target.HasComponent<PerceptionTargetComponent>())
			return result;

		const auto& perception = observer.GetComponent<PerceptionComponent>();
		const auto& targetComponent = target.GetComponent<PerceptionTargetComponent>();

		std::vector<UUID> observerIgnore, targetIgnore;
		CollectHierarchy(observer, observerIgnore);
		CollectHierarchy(target, targetIgnore);
		std::vector<UUID> bothIgnore = observerIgnore;
		bothIgnore.insert(bothIgnore.end(), targetIgnore.begin(), targetIgnore.end());

		const glm::quat observerRotation = m_Scene.GetWorldRotation(observer);
		const glm::vec3 eye = m_Scene.GetWorldPosition(observer) + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
		const glm::vec3 forward = glm::normalize(Math::Forward(observerRotation));
		const float halfFov = glm::radians(std::clamp(perception.FieldOfView, 1.0f, 359.0f) * 0.5f);

		const glm::vec3 targetPosition = m_Scene.GetWorldPosition(target);
		const glm::mat4 targetFrame = Math::ComposeTransform(targetPosition, m_Scene.GetWorldRotation(target), glm::vec3(1.0f));

		auto sightFilter = [&](UUID entity) { return IsSightBlocker(entity, bothIgnore); };

		float contrastSum = 0.0f, targetLumSum = 0.0f, backgroundLumSum = 0.0f, angleSum = 0.0f, distanceSum = 0.0f, opticalSum = 0.0f;
		result.TotalPoints = static_cast<uint32_t>(targetComponent.SilhouettePoints.size());

		for (const glm::vec3& localPoint : targetComponent.SilhouettePoints)
		{
			const glm::vec3 point = glm::vec3(targetFrame * glm::vec4(localPoint, 1.0f));
			glm::vec3 toPoint = point - eye;
			const float distance = glm::length(toPoint);
			if (distance < Math::Epsilon || distance > perception.ViewDistance)
				continue;
			toPoint /= distance;
			const float angle = std::acos(std::clamp(glm::dot(forward, toPoint), -1.0f, 1.0f));
			if (angle > halfFov)
				continue;

			// 1. Is the point itself visible from the eye?
			if (physics->Raycast(eye, toPoint, distance, sightFilter))
				continue;

			// 2. Target luminance: light falling on the side of the target facing the observer.
			const float illuminance = m_Provider->Illuminance(point, -toPoint, targetIgnore);
			const float lp = PerceptionModel::SurfaceLuminance(targetComponent.Reflectance, illuminance, 0.0f);

			// 3. Background luminance: continue the sight line through the target.
			float lb = m_Provider->SkyLuminance();
			const float remaining = perception.ViewDistance * BackgroundRangeMultiplier;
			if (std::optional<RaycastHit> hit = physics->Raycast(point, toPoint, remaining, sightFilter))
			{
				const SurfaceSample surface = m_Provider->GetSurface(hit->Entity);
				const float backgroundIlluminance = m_Provider->Illuminance(hit->Point + hit->Normal * 0.02f, hit->Normal, { hit->Entity });
				lb = PerceptionModel::SurfaceLuminance(surface.Albedo, backgroundIlluminance, surface.Emissive);
			}

			result.VisiblePoints++;
			contrastSum += PerceptionModel::WeberContrast(lb, lp, perception.LuminanceFloor);
			targetLumSum += lp;
			backgroundLumSum += lb;
			angleSum += angle;
			distanceSum += distance;
			opticalSum += OpticalDepth(eye, point);
		}

		if (result.VisiblePoints == 0 || result.TotalPoints == 0)
		{
			result.ContrastThreshold = PerceptionModel::ContrastThreshold(perception.RuntimeAdaptation, 0.0f, perception.ContrastMin,
				perception.DarkSensitivity, perception.GlareSensitivity);
			return result;
		}

		const float visible = static_cast<float>(result.VisiblePoints);
		result.VisibleFraction = visible / static_cast<float>(result.TotalPoints);
		result.TargetLuminance = targetLumSum / visible;
		result.BackgroundLuminance = backgroundLumSum / visible;
		result.ContrastThreshold = PerceptionModel::ContrastThreshold(perception.RuntimeAdaptation,
			std::max(result.TargetLuminance, result.BackgroundLuminance), perception.ContrastMin, perception.DarkSensitivity,
			perception.GlareSensitivity);
		result.Distance = distanceSum / visible;
		result.Atmosphere = PerceptionModel::AtmosphereFactor(opticalSum / visible);
		result.Contrast = (contrastSum / visible) * result.Atmosphere;
		result.SizeFactor = PerceptionModel::SizeFactor(targetComponent.Radius, result.Distance, perception.ReferenceAngularSize);

		const float peripherality = std::clamp((angleSum / visible) / halfFov, 0.0f, 1.0f);
		result.Peripheral = PerceptionModel::PeripheralFactor(peripherality, perception.PeripheralFactor);
		result.Motion = PerceptionModel::MotionFactor(glm::length(targetComponent.RuntimeVelocity), peripherality, perception.MotionFactor);
		result.Stimulus = PerceptionModel::Stimulus(result.Contrast, result.ContrastThreshold, result.VisibleFraction, result.SizeFactor,
			result.Peripheral, result.Motion);
		return result;
	}

	float PerceptionSystem::EnvironmentLuminanceFor(Entity observer, float extraLuminanceSum, uint32_t extraSamples)
	{
		const auto& perception = observer.GetComponent<PerceptionComponent>();
		const glm::vec3 eye = m_Scene.GetWorldPosition(observer) + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
		std::vector<UUID> ignore;
		CollectHierarchy(observer, ignore);

		const float here = PerceptionModel::SurfaceLuminance(NeutralAlbedo, m_Provider->Illuminance(eye, glm::vec3(0.0f), ignore), 0.0f);

		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		if (!physics)
			return here;

		// What the eye looks at: fixed probes around the view direction plus the visible target
		// samples (a brightly lit intruder in view brightens the adaptation like anything else).
		const glm::quat rotation = m_Scene.GetWorldRotation(observer);
		float viewSum = 0.0f;
		for (const glm::vec2& probe : s_ViewProbes)
		{
			const glm::quat offset = Math::QuatFromEulerDegrees(glm::vec3(probe.y, probe.x, 0.0f));
			const glm::vec3 direction = glm::normalize(Math::Forward(rotation * offset));
			float luminance = m_Provider->SkyLuminance();
			auto filter = [&](UUID entity) { return IsSightBlocker(entity, ignore); };
			if (std::optional<RaycastHit> hit = physics->Raycast(eye, direction, perception.ViewDistance * BackgroundRangeMultiplier, filter))
			{
				const SurfaceSample surface = m_Provider->GetSurface(hit->Entity);
				const float illuminance = m_Provider->Illuminance(hit->Point + hit->Normal * 0.02f, hit->Normal, { hit->Entity });
				luminance = PerceptionModel::SurfaceLuminance(surface.Albedo, illuminance, surface.Emissive);
			}
			viewSum += luminance;
		}
		const float viewMean = (viewSum + extraLuminanceSum) / static_cast<float>(s_ViewProbes.size() + extraSamples);
		return 0.5f * (here + viewMean);
	}

	void PerceptionSystem::UpdateTargets(float dt)
	{
		for (auto [handle, target] : m_Scene.GetRegistry().view<PerceptionTargetComponent>().each())
		{
			const glm::vec3 position = m_Scene.GetWorldPosition(Entity(handle, &m_Scene));
			if (target.RuntimeHasPreviousPosition && dt > 0.0f)
			{
				const glm::vec3 velocity = (position - target.RuntimePreviousPosition) / dt;
				// Light smoothing avoids single-frame spikes from physics corrections.
				target.RuntimeVelocity = glm::mix(target.RuntimeVelocity, velocity, 0.5f);
			}
			target.RuntimePreviousPosition = position;
			target.RuntimeHasPreviousPosition = true;
			target.RuntimeExposure = 0.0f;
		}
	}

	void PerceptionSystem::ProcessNoises()
	{
		m_RecentNoises = std::move(m_PendingNoises);
		m_PendingNoises.clear();
		if (m_RecentNoises.empty())
			return;

		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		for (auto [handle, perception] : m_Scene.GetRegistry().view<PerceptionComponent>().each())
		{
			Entity observer(handle, &m_Scene);
			std::vector<UUID> ignore;
			CollectHierarchy(observer, ignore);
			const glm::vec3 ear = m_Scene.GetWorldPosition(observer) + glm::vec3(0.0f, perception.EyeHeight, 0.0f);

			for (const NoiseEvent& noise : m_RecentNoises)
			{
				if (Contains(ignore, noise.Source))
					continue;
				float radius = noise.Radius * perception.HearingSensitivity;
				const glm::vec3 toEar = ear - noise.Position;
				const float distance = glm::length(toEar);
				if (distance >= radius)
					continue;
				// Walls muffle sound: occluded noises carry half as far.
				if (physics && distance > Math::Epsilon)
				{
					auto filter = [&](UUID entity) { return entity != noise.Source && IsSightBlocker(entity, ignore); };
					if (physics->Raycast(noise.Position, toEar / distance, distance, filter))
						radius *= 0.5f;
				}
				if (distance >= radius)
					continue;

				const float strength = 1.0f - distance / radius;
				perception.RuntimeSuspicion = std::clamp(perception.RuntimeSuspicion + perception.HearingSuspicion * strength, 0.0f, 1.0f);
				perception.RuntimeLastStimulusPosition = noise.Position;
				perception.RuntimeHasStimulus = true;
			}
		}
	}

	void PerceptionSystem::UpdateObserver(Entity observer, float dt, float visionElapsed)
	{
		auto& perception = observer.GetComponent<PerceptionComponent>();

		if (visionElapsed > 0.0f)
		{
			float bestStimulus = 0.0f;
			VisibilityResult best;
			Entity bestTarget;
			for (auto [handle, targetComponent] : m_Scene.GetRegistry().view<PerceptionTargetComponent>().each())
			{
				Entity target(handle, &m_Scene);
				if (target == observer)
					continue;
				const VisibilityResult visibility = ComputeVisibility(observer, target);
				targetComponent.RuntimeExposure = std::max(targetComponent.RuntimeExposure, visibility.Stimulus);
				if (visibility.VisiblePoints > 0)
					targetComponent.RuntimeLightLevel = visibility.TargetLuminance;
				if (!bestTarget || visibility.Stimulus > bestStimulus)
				{
					bestStimulus = visibility.Stimulus;
					best = visibility;
					bestTarget = target;
				}
			}

			// Adapt after evaluating, so this evaluation used the adaptation of the previous moment.
			const float samples = static_cast<float>(best.VisiblePoints);
			const float environment = EnvironmentLuminanceFor(observer, (best.TargetLuminance + best.BackgroundLuminance) * samples,
				best.VisiblePoints * 2);
			perception.RuntimeAdaptation = PerceptionModel::Adapt(perception.RuntimeAdaptation, environment, visionElapsed,
				perception.AdaptBrightTime, perception.AdaptDarkTime);

			perception.RuntimeStimulus = best.Stimulus;
			perception.RuntimeVisibleFraction = best.VisibleFraction;
			perception.RuntimeContrast = best.Contrast;
			if (bestTarget && best.Stimulus >= 1.0f)
			{
				perception.RuntimeLastStimulusPosition = m_Scene.GetWorldPosition(bestTarget);
				perception.RuntimeHasStimulus = true;
				perception.RuntimeLastTarget = bestTarget.GetUUID();
			}
		}

		perception.RuntimeSuspicion = PerceptionModel::UpdateSuspicion(perception.RuntimeSuspicion, perception.RuntimeStimulus, dt,
			perception.SuspicionRise, perception.SuspicionDecay);
		perception.RuntimeState =
			PerceptionModel::StateFromSuspicion(perception.RuntimeSuspicion, perception.SuspiciousThreshold, perception.SearchingThreshold);
		if (perception.RuntimeState == AwarenessState::Unaware && perception.RuntimeSuspicion <= 0.0f)
			perception.RuntimeHasStimulus = false;
	}

	void PerceptionSystem::Update(Timestep ts)
	{
		m_Time += ts;
		m_Provider->BeginFrame(m_Time);
		UpdateTargets(ts);
		ProcessNoises();

		m_VisionTimer += ts;
		const float visionElapsed = m_VisionTimer;
		const bool evaluateVision = m_VisionTimer >= VisionInterval;
		if (evaluateVision)
			m_VisionTimer = 0.0f;

		for (entt::entity handle : m_Scene.GetRegistry().view<PerceptionComponent>())
			UpdateObserver(Entity(handle, &m_Scene), ts, evaluateVision ? visionElapsed : 0.0f);
	}

}
