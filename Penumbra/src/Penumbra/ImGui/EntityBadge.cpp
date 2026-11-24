#include "pnpch.h"
#include "Penumbra/ImGui/EntityBadge.h"

#include "Penumbra/Scene/Components.h"

namespace Penumbra {

	namespace {

		constexpr uint32_t Rgb(uint32_t r, uint32_t g, uint32_t b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }

	}

	EntityBadge GetEntityBadge(Entity entity)
	{
		const PerceptionTargetComponent* target = entity.TryGetComponent<PerceptionTargetComponent>();
		if (entity.HasComponent<PerceptionComponent>())
			return { 'G', Rgb(230, 95, 85), "Guard (Perception)" };
		if (entity.HasComponent<FogOfWarComponent>() || (target && target->Kind == TargetKind::Intruder))
			return { 'T', Rgb(120, 210, 120), "Thief" };
		if (entity.HasComponent<PointLightComponent>() || entity.HasComponent<SpotLightComponent>() ||
			entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<FlameComponent>())
			return { 'L', Rgb(240, 190, 80), "Light" };
		if (entity.HasComponent<CameraComponent>())
			return { 'C', Rgb(110, 200, 220), "Camera" };
		if (entity.HasComponent<EnvironmentComponent>())
			return { 'E', Rgb(120, 150, 240), "Environment" };
		if (entity.HasComponent<ScriptComponent>())
			return { 'S', Rgb(90, 200, 170), "Scripted" };
		if (entity.HasComponent<PrefabComponent>())
			return { 'P', Rgb(190, 140, 230), "Prefab instance" };
		if (entity.HasComponent<AudioSourceComponent>())
			return { 'A', Rgb(220, 130, 200), "Sound" };
		if (entity.HasComponent<RigidBodyComponent>() || entity.HasComponent<CharacterControllerComponent>())
			return { 'B', Rgb(150, 170, 190), "Physics body" };
		if (entity.HasComponent<MeshComponent>())
			return { 'M', Rgb(175, 175, 175), "Mesh" };
		return {};
	}

}
