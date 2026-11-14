#include "pnpch.h"
#include "Penumbra/AI/PerceptionSystem.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Scene/JsonSerialization.h"
#include "Penumbra/Physics/PhysicsWorld.h"

#include <glm/gtc/constants.hpp>

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
		: m_Scene(scene), m_Provider(CreateScope<AnalyticLuminanceProvider>(scene))
	{
	}

	void PerceptionSystem::EmitNoise(const glm::vec3& position, float loudness, UUID source, std::string cause)
	{
		if (loudness > 0.0f)
			m_PendingNoises.push_back({ position, std::min(loudness, 10.0f), source, std::move(cause) });
	}

	float PerceptionSystem::HearingLevel(Entity observer, const glm::vec3& position, float loudness, UUID source)
	{
		const auto& perception = observer.GetComponent<PerceptionComponent>();
		// Alert memory sharpens the ears: up to 50% further.
		const float sensitivity = std::max(perception.HearingSensitivity * (1.0f + 0.5f * perception.RuntimeAlertness), 1e-3f);
		const glm::vec3 feet = m_Scene.GetWorldPosition(observer);
		const glm::vec3 ear = feet + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
		const glm::vec3 toEar = ear - position;
		const float distance = glm::length(toEar);
		if (distance / sensitivity >= PerceptionModel::NoiseRange(loudness))
			return 0.0f;

		const float direct = PerceptionModel::HeardLevel(loudness, distance / sensitivity);
		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		if (!physics || distance <= Math::Epsilon)
			return direct;
		std::vector<UUID> ignore;
		CollectHierarchy(observer, ignore);
		auto filter = [&](UUID entity) { return entity != source && IsSightBlocker(entity, ignore); };
		// Noises are often made on a surface (footsteps, impacts): start the occlusion test a
		// little above it so the floor itself does not count as a wall.
		const glm::vec3 origin = position + glm::vec3(0.0f, 0.15f, 0.0f);
		const std::optional<float> through = TransmissionLoss(origin, ear, filter, false);
		if (through && *through <= 0.0f)
			return direct;

		// Occluded: through closed doors (muffled by each), through a wall (heavily muffled; never
		// worse than that), or around along walkable space, which stands in for the doorways and
		// corridors sound travels through. The navmesh runs through closed doors: they cost their
		// SoundLoss on the way.
		float level = direct - std::min(through.value_or(PerceptionModel::WallLoss), PerceptionModel::WallLoss);
		const NavigationSystem* navigation = m_Scene.GetNavigationSystem();
		if (const NavMesh* navMesh = navigation ? navigation->GetBakedNavMesh() : nullptr)
		{
			if (std::optional<NavPath> path = navMesh->FindPath(position, feet); path && path->Complete)
			{
				float doors = 0.0f;
				const glm::vec3 lift(0.0f, 1.0f, 0.0f);
				for (size_t i = 1; i < path->Points.size(); i++)
					doors += TransmissionLoss(path->Points[i - 1] + lift, path->Points[i] + lift, filter, true).value_or(0.0f);
				level = std::max(level, PerceptionModel::HeardLevel(loudness, path->Length / sensitivity) - PerceptionModel::CornerLoss - doors);
			}
		}
		return level;
	}

	bool PerceptionSystem::IsAwake(Entity entity)
	{
		const VitalityComponent* vitality = entity.TryGetComponent<VitalityComponent>();
		return !vitality || vitality->State == VitalState::Awake;
	}

	bool PerceptionSystem::KnockOut(Entity npc, VitalState state)
	{
		if (state == VitalState::Awake || !IsAwake(npc))
			return false;
		auto& vitality = npc.HasComponent<VitalityComponent>() ? npc.GetComponent<VitalityComponent>() : npc.AddComponent<VitalityComponent>();
		vitality.State = state;
		if (PerceptionComponent* perception = npc.TryGetComponent<PerceptionComponent>())
			Calm(*perception);
		if (NavigationSystem* navigation = m_Scene.GetNavigationSystem())
			navigation->Stop(npc);
		if (npc.HasComponent<CharacterControllerComponent>())
		{
			if (PhysicsWorld* physics = m_Scene.GetPhysicsWorld())
				physics->RemoveCharacter(npc);
			npc.RemoveComponent<CharacterControllerComponent>();
		}

		// A body on the floor: low, long silhouette points around where it fell.
		auto& target = npc.HasComponent<PerceptionTargetComponent>() ? npc.GetComponent<PerceptionTargetComponent>()
																	   : npc.AddComponent<PerceptionTargetComponent>();
		target.Kind = TargetKind::Body;
		target.SilhouettePoints = { { 0.0f, 0.25f, 0.0f }, { 0.0f, 0.2f, 0.45f }, { 0.0f, 0.2f, -0.45f }, { 0.0f, 0.15f, 0.85f },
			{ 0.0f, 0.15f, -0.85f }, { 0.3f, 0.2f, 0.0f }, { -0.3f, 0.2f, 0.0f } };
		target.Radius = 0.4f;
		target.Reflectance = 0.15f; // dark clothes and armour against the floor
		PN_CORE_TRACE("'{}' is {}", npc.GetName(), EnumToString(state));
		return true;
	}

	float PerceptionSystem::Damage(Entity npc, float amount)
	{
		auto& vitality = npc.HasComponent<VitalityComponent>() ? npc.GetComponent<VitalityComponent>() : npc.AddComponent<VitalityComponent>();
		if (vitality.State == VitalState::Dead || amount <= 0.0f)
			return vitality.Health;
		vitality.Health = std::max(vitality.Health - amount, 0.0f);
		PN_CORE_TRACE("'{}' takes {:.0f} damage ({:.0f} left)", npc.GetName(), amount, vitality.Health);
		if (vitality.Health <= 0.0f)
		{
			if (!KnockOut(npc, VitalState::Dead))
				vitality.State = VitalState::Dead; // already down: an unconscious body is finished off
		}
		return vitality.Health;
	}

	std::vector<Entity> PerceptionSystem::Shout(Entity speaker, float loudness)
	{
		std::vector<Entity> alerted;
		const PerceptionComponent* said = speaker.TryGetComponent<PerceptionComponent>();
		if (!said || !IsAwake(speaker) || !said->RuntimeHasStimulus || said->RuntimeReactionStrength <= 0.0f)
			return alerted;
		const glm::vec3 mouth = m_Scene.GetWorldPosition(speaker) + glm::vec3(0.0f, said->EyeHeight, 0.0f);
		const float strength = std::max(said->RuntimeReactionStrength - 1.0f, 0.0f); // second-hand news
		const glm::vec3 where = said->RuntimeLastStimulusPosition;
		float masking = 0.0f;
		if (Entity environment = m_Scene.GetEnvironmentEntity())
			masking = std::max(environment.GetComponent<EnvironmentComponent>().AmbientNoise, 0.0f);

		std::vector<Entity> listeners;
		for (entt::entity handle : m_Scene.GetRegistry().view<PerceptionComponent>())
			listeners.emplace_back(handle, &m_Scene);
		std::sort(listeners.begin(), listeners.end(),
			[](Entity a, Entity b) { return static_cast<uint64_t>(a.GetUUID()) < static_cast<uint64_t>(b.GetUUID()); });
		for (Entity listener : listeners)
		{
			if (listener == speaker || !IsAwake(listener))
				continue;
			if (HearingLevel(listener, mouth, loudness, speaker.GetUUID()) - masking <= 0.0f)
				continue;
			if (Notify(listener, where, strength, StimulusKind::Reported, "Shout"))
				alerted.push_back(listener);
		}
		return alerted;
	}

	bool PerceptionSystem::Notify(Entity observer, const glm::vec3& position, float strength, StimulusKind kind, std::string_view cause)
	{
		if (!IsAwake(observer))
			return false;
		auto& perception = observer.GetComponent<PerceptionComponent>();
		strength = std::clamp(strength, 0.0f, 10.0f);
		const float duration = PerceptionModel::ReactionDuration(strength, perception.ReactionDuration);
		const float current = PerceptionModel::ReactionStrength(perception.RuntimeReactionTime, perception.ReactionDuration);
		if (duration <= 0.0f || strength + 1e-4f < current)
			return false;

		perception.RuntimeReactionTime = std::max(perception.RuntimeReactionTime, duration);
		perception.RuntimeReactionStrength = PerceptionModel::ReactionStrength(perception.RuntimeReactionTime, perception.ReactionDuration);
		perception.RuntimeState = PerceptionModel::StateFromStrength(perception.RuntimeReactionStrength, perception.InvestigateLevel,
			perception.SearchLevel, perception.AlarmLevel);
		perception.RuntimeLastStimulusPosition = position;
		perception.RuntimeHasStimulus = true;
		perception.RuntimeStimulusKind = kind;
		perception.RuntimeStimulusCause = std::string(cause);
		if (strength >= perception.SearchLevel)
			perception.RuntimeAlertness = std::max(perception.RuntimeAlertness, strength / 10.0f);
		return true;
	}

	std::vector<Entity> PerceptionSystem::ExplainAway(float strength, std::string_view cause, std::optional<glm::vec3> center, float radius,
		float spread)
	{
		std::vector<Entity> calmed;
		std::vector<Entity> observers;
		for (entt::entity handle : m_Scene.GetRegistry().view<PerceptionComponent>())
			observers.emplace_back(handle, &m_Scene);
		// Stable order so the rolls go to the same NPCs every run.
		std::sort(observers.begin(), observers.end(),
			[](Entity a, Entity b) { return static_cast<uint64_t>(a.GetUUID()) < static_cast<uint64_t>(b.GetUUID()); });

		for (Entity observer : observers)
		{
			auto& perception = observer.GetComponent<PerceptionComponent>();
			if (!IsAwake(observer) || perception.RuntimeReactionTime <= 0.0f || perception.RuntimeStimulusKind == StimulusKind::Seen)
				continue;
			if (center && glm::distance(m_Scene.GetWorldPosition(observer), *center) > radius)
				continue;
			const float roll = spread > 0.0f ? m_Scene.NextRandom() * 2.0f - 1.0f : 0.0f;
			if (!PerceptionModel::DistractionWins(strength, perception.RuntimeReactionStrength, perception.Diligence, spread, roll))
				continue;
			PN_CORE_TRACE("'{}' puts it down to {} ({:.1f} vs {:.1f})", observer.GetName(), cause, strength,
				perception.RuntimeReactionStrength + perception.Diligence);
			Calm(perception);
			calmed.push_back(observer);
		}
		return calmed;
	}

	void PerceptionSystem::Calm(PerceptionComponent& perception)
	{
		perception.RuntimeSuspicion = 0.0f;
		perception.RuntimeReactionTime = 0.0f;
		perception.RuntimeReactionStrength = 0.0f;
		perception.RuntimeState = AwarenessState::Unaware;
		perception.RuntimeHasStimulus = false;
		perception.RuntimeStimulusKind = StimulusKind::None;
		perception.RuntimeStimulusCause.clear();
	}

	float PerceptionSystem::SoundLossOf(UUID entityID)
	{
		std::optional<Entity> entity = m_Scene.TryGetEntityByUUID(entityID);
		const SurfaceComponent* surface = entity ? entity->TryGetComponent<SurfaceComponent>() : nullptr;
		return surface ? surface->SoundLoss : -1.0f;
	}

	std::optional<float> PerceptionSystem::TransmissionLoss(const glm::vec3& from, const glm::vec3& to, const std::function<bool(UUID)>& blocker, bool doorsOnly)
	{
		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		const glm::vec3 offset = to - from;
		const float distance = glm::length(offset);
		if (!physics || distance <= Math::Epsilon)
			return 0.0f;
		const glm::vec3 direction = offset / distance;
		std::vector<UUID> passed;
		float loss = 0.0f;
		auto filter = [&](UUID entity) {
			if (Contains(passed, entity) || !blocker(entity))
				return false;
			return !doorsOnly || SoundLossOf(entity) >= 0.0f;
		};
		// A few doors at most between two points; more than that is a wall.
		for (int door = 0; door < 4; door++)
		{
			const std::optional<RaycastHit> hit = physics->Raycast(from, direction, distance, filter);
			if (!hit)
				return loss;
			const float doorLoss = SoundLossOf(hit->Entity);
			if (doorLoss < 0.0f)
				return std::nullopt;
			loss += doorLoss;
			passed.push_back(hit->Entity);
		}
		return doorsOnly ? std::optional<float>(loss) : std::nullopt;
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

	float PerceptionSystem::HorizontalDistance(Entity a, Entity b)
	{
		const glm::vec3 delta = m_Scene.GetWorldPosition(b) - m_Scene.GetWorldPosition(a);
		return glm::length(glm::vec2(delta.x, delta.z));
	}

	bool PerceptionSystem::IsTouching(Entity observer, Entity target)
	{
		const PerceptionComponent* perception = observer.TryGetComponent<PerceptionComponent>();
		if (!perception || observer == target)
			return false;
		// Characters stand on their origin; anything else is taken as a person-sized upright capsule.
		constexpr float DefaultHeight = 1.8f;
		auto capsule = [](Entity entity, float radius)
		{
			if (const auto* controller = entity.TryGetComponent<CharacterControllerComponent>())
				return glm::vec2(controller->Radius, controller->Height);
			return glm::vec2(radius, DefaultHeight);
		};
		const PerceptionTargetComponent* targetComponent = target.TryGetComponent<PerceptionTargetComponent>();
		const glm::vec2 observerCapsule = capsule(observer, 0.3f);
		const glm::vec2 targetCapsule = capsule(target, targetComponent ? targetComponent->Radius : 0.3f);

		const float observerBase = m_Scene.GetWorldPosition(observer).y;
		const float targetBase = m_Scene.GetWorldPosition(target).y;
		if (targetBase > observerBase + observerCapsule.y || observerBase > targetBase + targetCapsule.y)
			return false;
		return HorizontalDistance(observer, target) <= observerCapsule.x + targetCapsule.x + std::max(perception->TouchRange, 0.0f);
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
		const TargetLighting& lighting = GetTargetLighting(target);

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

			// 2. Target luminance: the body is rounded, so it is lit by everything around it as seen
			//    from this observer, plus light bounced up from the floor it stands on.
			const size_t pointIndex = static_cast<size_t>(&localPoint - targetComponent.SilhouettePoints.data());
			const float illuminance = lighting.Points[pointIndex].Rounded(-toPoint) + lighting.Bounce;
			const float lp = PerceptionModel::SurfaceLuminance(targetComponent.Reflectance, illuminance, targetComponent.Emissive);

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

		// Right in front of him a figure is made out even in the dark (slowly: suspicion still has to rise).
		if (targetComponent.Kind == TargetKind::Intruder && HorizontalDistance(observer, target) <= perception.CloseRange)
			result.Stimulus = std::max(result.Stimulus, perception.CloseRangeStimulus * result.VisibleFraction * result.Peripheral);
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

	const PerceptionSystem::TargetLighting& PerceptionSystem::GetTargetLighting(Entity target)
	{
		TargetLighting& lighting = m_TargetLighting[target.GetUUID()];
		const auto& component = target.GetComponent<PerceptionTargetComponent>();
		// Refresh when stale, or when the silhouette changed shape (e.g. crouching) since the last gather.
		if (lighting.Time >= 0.0f && m_Time - lighting.Time < LightingRefresh && lighting.Points.size() == component.SilhouettePoints.size())
			return lighting;
		lighting.Time = m_Time;
		lighting.Points.clear();
		lighting.Bounce = 0.0f;

		std::vector<UUID> ignore;
		CollectHierarchy(target, ignore);
		const glm::vec3 position = m_Scene.GetWorldPosition(target);
		const glm::mat4 frame = Math::ComposeTransform(position, m_Scene.GetWorldRotation(target), glm::vec3(1.0f));
		lighting.Points.reserve(component.SilhouettePoints.size());
		for (const glm::vec3& localPoint : component.SilhouettePoints)
			lighting.Points.push_back(m_Provider->GatherLighting(glm::vec3(frame * glm::vec4(localPoint, 1.0f)), ignore));

		// A brightly lit floor lights the figure standing on it from below (one bounce,
		// half of the body's view is the floor).
		if (const PhysicsWorld* physics = m_Scene.GetPhysicsWorld())
		{
			auto filter = [&](UUID entity) { return IsSightBlocker(entity, ignore); };
			if (std::optional<RaycastHit> floor = physics->Raycast(position + glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1.5f, filter))
			{
				const SurfaceSample surface = m_Provider->GetSurface(floor->Entity);
				const float floorIlluminance = m_Provider->Illuminance(floor->Point + floor->Normal * 0.02f, floor->Normal, { floor->Entity });
				const float floorLuminance = PerceptionModel::SurfaceLuminance(surface.Albedo, floorIlluminance, surface.Emissive);
				lighting.Bounce = glm::pi<float>() * floorLuminance * 0.5f;
			}
		}
		return lighting;
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
			if (!target.RuntimeHasPreviousPosition)
				target.RuntimeOrigin = position;
			target.RuntimePreviousPosition = position;
			target.RuntimeHasPreviousPosition = true;

			// Light gem: how much light the body stands in, regardless of who is looking.
			Entity entity(handle, &m_Scene);
			const TargetLighting& lighting = GetTargetLighting(entity);
			float average = 0.0f;
			for (const PointLighting& point : lighting.Points)
				average += point.Average();
			if (!lighting.Points.empty())
				average /= static_cast<float>(lighting.Points.size());
			target.RuntimeLightLevel = PerceptionModel::SurfaceLuminance(target.Reflectance, average + lighting.Bounce, 0.0f);
			target.RuntimeLightGem = PerceptionModel::LightGem(target.RuntimeLightLevel);

			// Exposure: the strongest stimulus any observer currently registers for this target.
			float exposure = 0.0f;
			for (const auto& [observer, focus] : m_Focus)
			{
				if (focus.Target == entity.GetUUID() && m_Scene.TryGetEntityByUUID(observer))
					exposure = std::max(exposure, focus.Stimulus);
			}
			target.RuntimeExposure = exposure;
		}
	}

	void PerceptionSystem::ProcessNoises()
	{
		m_RecentNoises = std::move(m_PendingNoises);
		m_PendingNoises.clear();
		if (m_RecentNoises.empty())
			return;

		// Background noise (rain, machinery...) masks quieter sounds.
		float masking = 0.0f;
		if (Entity environment = m_Scene.GetEnvironmentEntity())
			masking = std::max(environment.GetComponent<EnvironmentComponent>().AmbientNoise, 0.0f);

		for (entt::entity handle : m_Scene.GetRegistry().view<PerceptionComponent>())
		{
			Entity observer(handle, &m_Scene);
			if (!IsAwake(observer))
				continue;
			std::vector<UUID> ignore;
			CollectHierarchy(observer, ignore);
			for (const NoiseEvent& noise : m_RecentNoises)
			{
				if (Contains(ignore, noise.Source))
					continue;
				const float level = HearingLevel(observer, noise.Position, noise.Loudness, noise.Source) - masking;
				if (level > 0.0f)
					Notify(observer, noise.Position, level, StimulusKind::Heard, noise.Cause.empty() ? std::string_view("Noise") : noise.Cause);
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
			std::vector<Entity> bodies;
			for (auto [handle, targetComponent] : m_Scene.GetRegistry().view<PerceptionTargetComponent>().each())
			{
				Entity target(handle, &m_Scene);
				if (target == observer)
					continue;
				if (targetComponent.Kind == TargetKind::Body)
				{
					bodies.push_back(target);
					continue;
				}
				if (targetComponent.Kind == TargetKind::Projectile)
					continue; // judged every frame below
				const VisibilityResult visibility = ComputeVisibility(observer, target);
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
			m_Focus[observer.GetUUID()] = { bestTarget ? bestTarget.GetUUID() : UUID(UUID::Invalid), best.Stimulus };
			perception.RuntimeVisibleFraction = best.VisibleFraction;
			perception.RuntimeContrast = best.Contrast;

			// Bodies are judged by the same eyes (a body in the dark goes unnoticed) and found once.
			std::unordered_set<UUID>& noticed = m_Noticed[observer.GetUUID()];
			for (Entity body : bodies)
			{
				if (noticed.contains(body.GetUUID()) || ComputeVisibility(observer, body).Stimulus < 1.0f)
					continue;
				noticed.insert(body.GetUUID());
				Notify(observer, m_Scene.GetWorldPosition(body), perception.BodyAlarm, StimulusKind::Seen, "Body");
			}
			m_Seeing[observer.GetUUID()] = bestTarget && best.Stimulus >= 1.0f ? bestTarget.GetUUID() : UUID(UUID::Invalid);
		}

		// Arrows cross a view cone between two vision updates: they are judged every frame. Seen
		// once, an arrow sends the observer to where it came from.
		for (auto [handle, targetComponent] : m_Scene.GetRegistry().view<PerceptionTargetComponent>().each())
		{
			if (targetComponent.Kind != TargetKind::Projectile || !targetComponent.RuntimeHasPreviousPosition)
				continue;
			Entity projectile(handle, &m_Scene);
			std::unordered_set<UUID>& noticed = m_Noticed[observer.GetUUID()];
			if (noticed.contains(projectile.GetUUID()) || ComputeVisibility(observer, projectile).Stimulus < 1.0f)
				continue;
			noticed.insert(projectile.GetUUID());
			Notify(observer, targetComponent.RuntimeOrigin, perception.ProjectileAlarm, StimulusKind::Seen, "Arrow");
		}

		// Touch beats shadow: an intruder he bumps into, or who bumps into him, is noticed at once
		// whatever the light, and he knows exactly where.
		for (auto [handle, targetComponent] : m_Scene.GetRegistry().view<PerceptionTargetComponent>().each())
		{
			Entity target(handle, &m_Scene);
			if (targetComponent.Kind != TargetKind::Intruder || !IsTouching(observer, target))
				continue;
			if (Notify(observer, m_Scene.GetWorldPosition(target), perception.TouchAlarm, StimulusKind::Seen, "Touch"))
			{
				perception.RuntimeSuspicion = 1.0f;
				perception.RuntimeLastTarget = target.GetUUID();
			}
		}

		// Alert memory makes a guard notice faster.
		perception.RuntimeSuspicion = PerceptionModel::UpdateSuspicion(perception.RuntimeSuspicion, perception.RuntimeStimulus, dt,
			perception.SuspicionRise * (1.0f + perception.RuntimeAlertness), perception.SuspicionDecay);

		// The reaction fades: Alarmed -> Searching -> Investigating -> Suspicious -> Unaware.
		perception.RuntimeReactionTime = std::max(perception.RuntimeReactionTime - dt, 0.0f);

		// Something seen clearly enough is a stimulus of strength 10 * suspicion; while the target
		// is in sight the observer keeps track of where it is.
		const auto seeing = m_Seeing.find(observer.GetUUID());
		std::optional<Entity> seen = seeing != m_Seeing.end() ? m_Scene.TryGetEntityByUUID(seeing->second) : std::nullopt;
		if (seen)
		{
			const glm::vec3 position = m_Scene.GetWorldPosition(*seen);
			perception.RuntimeLastTarget = seen->GetUUID();
			const bool taken = perception.RuntimeSuspicion >= perception.SuspiciousThreshold &&
				Notify(observer, position, 10.0f * perception.RuntimeSuspicion, StimulusKind::Seen, "Seen");
			if (!taken)
			{
				perception.RuntimeLastStimulusPosition = position;
				perception.RuntimeHasStimulus = true;
				if (perception.RuntimeReactionTime > 0.0f)
				{
					perception.RuntimeStimulusKind = StimulusKind::Seen;
					perception.RuntimeStimulusCause = "Seen";
				}
			}
		}

		perception.RuntimeReactionStrength = PerceptionModel::ReactionStrength(perception.RuntimeReactionTime, perception.ReactionDuration);
		perception.RuntimeState = PerceptionModel::StateFromStrength(perception.RuntimeReactionStrength, perception.InvestigateLevel,
			perception.SearchLevel, perception.AlarmLevel);
		if (perception.RuntimeState == AwarenessState::Unaware)
		{
			perception.RuntimeAlertness = std::max(perception.RuntimeAlertness - dt / std::max(perception.AlertMemoryTime, 1e-3f), 0.0f);
			if (perception.RuntimeSuspicion <= 0.0f && !seen)
			{
				perception.RuntimeHasStimulus = false;
				perception.RuntimeStimulusKind = StimulusKind::None;
				perception.RuntimeStimulusCause.clear();
			}
		}
	}

	void PerceptionSystem::Update(Timestep ts)
	{
		m_Time += ts;
		m_Provider->BeginFrame(m_Time);
		ProcessNoises();

		for (entt::entity handle : m_Scene.GetRegistry().view<PerceptionComponent>())
		{
			Entity observer(handle, &m_Scene);
			// Each observer evaluates vision on its own phase of the interval (spread by id), so
			// the cost is spread over frames instead of spiking every VisionInterval.
			auto [timer, inserted] = m_VisionTimers.try_emplace(observer.GetUUID(), 0.0f);
			if (inserted)
				timer->second = VisionInterval * (1.0f - static_cast<float>(static_cast<uint64_t>(observer.GetUUID()) % 1000) / 1000.0f);
			timer->second += ts;
			float visionElapsed = 0.0f;
			if (timer->second >= VisionInterval)
			{
				visionElapsed = timer->second;
				timer->second = 0.0f;
			}
			if (IsAwake(observer))
				UpdateObserver(observer, ts, visionElapsed);
		}

		// Targets last: velocity, light gem and the exposure from this frame's evaluations.
		UpdateTargets(ts);
		auto destroyed = [&](const auto& entry) { return !m_Scene.TryGetEntityByUUID(entry.first).has_value(); };
		std::erase_if(m_Focus, destroyed);
		std::erase_if(m_VisionTimers, destroyed);
		std::erase_if(m_Seeing, destroyed);
		std::erase_if(m_Noticed, destroyed);
		std::erase_if(m_TargetLighting, destroyed);
	}

}
