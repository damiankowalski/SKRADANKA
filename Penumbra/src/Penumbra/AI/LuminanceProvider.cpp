#include "pnpch.h"
#include "Penumbra/AI/LuminanceProvider.h"

#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scene/FogUtils.h"
#include "Penumbra/Scene/LightUtils.h"

#include <glm/gtc/constants.hpp>

namespace Penumbra {

	namespace {
		constexpr float ShadowBias = 0.03f;
		constexpr float DirectionalShadowDistance = 200.0f;

		void CollectRelated(Entity entity, std::vector<UUID>& related)
		{
			related.push_back(entity.GetUUID());
			for (Entity parent = entity.GetParent(); parent; parent = parent.GetParent())
				related.push_back(parent.GetUUID());
			std::vector<Entity> stack = entity.GetChildren();
			while (!stack.empty())
			{
				Entity child = stack.back();
				stack.pop_back();
				related.push_back(child.GetUUID());
				for (Entity grandChild : child.GetChildren())
					stack.push_back(grandChild);
			}
		}

		bool Contains(const std::vector<UUID>& list, UUID value) { return std::find(list.begin(), list.end(), value) != list.end(); }
	}

	void AnalyticLuminanceProvider::BeginFrame(float time)
	{
		m_Lights.clear();
		auto& registry = m_Scene.GetRegistry();

		for (auto [handle, light] : registry.view<DirectionalLightComponent>().each())
		{
			if (!light.Enabled)
				continue;
			Entity entity(handle, &m_Scene);
			LightSample sample;
			sample.Type = LightSample::Kind::Directional;
			sample.Owner = entity.GetUUID();
			sample.Direction = glm::normalize(Math::Forward(m_Scene.GetWorldRotation(entity)));
			sample.Intensity = Math::Luminance(light.Color) * light.Intensity;
			sample.CastsShadows = light.CastShadows;
			sample.RenderShadows = light.CastShadows;
			CollectRelated(entity, sample.Related);
			m_Lights.push_back(std::move(sample));
		}

		for (auto [handle, light] : registry.view<PointLightComponent>().each())
		{
			if (!light.Enabled)
				continue;
			Entity entity(handle, &m_Scene);
			LightSample sample;
			sample.Type = LightSample::Kind::Point;
			sample.Owner = entity.GetUUID();
			sample.Position = m_Scene.GetWorldPosition(entity);
			sample.Range = light.Range;
			sample.Intensity = Math::Luminance(light.Color) * light.Intensity * LightUtils::Flicker(time, sample.Owner, light.Flicker);
			// Point lights cast shadows for perception regardless of the render flag: NPCs must
			// never see through walls just because a light is not rendering shadow maps.
			sample.CastsShadows = true;
			sample.RenderShadows = light.CastShadows;
			CollectRelated(entity, sample.Related);
			m_Lights.push_back(std::move(sample));
		}

		for (auto [handle, light] : registry.view<SpotLightComponent>().each())
		{
			if (!light.Enabled)
				continue;
			Entity entity(handle, &m_Scene);
			LightSample sample;
			sample.Type = LightSample::Kind::Spot;
			sample.Owner = entity.GetUUID();
			sample.Position = m_Scene.GetWorldPosition(entity);
			sample.Direction = glm::normalize(Math::Forward(m_Scene.GetWorldRotation(entity)));
			sample.Range = light.Range;
			sample.Intensity = Math::Luminance(light.Color) * light.Intensity;
			sample.CosOuter = std::cos(glm::radians(light.OuterAngle));
			sample.CosInner = std::cos(glm::radians(std::min(light.InnerAngle, light.OuterAngle)));
			sample.CastsShadows = true;
			sample.RenderShadows = light.CastShadows;
			CollectRelated(entity, sample.Related);
			m_Lights.push_back(std::move(sample));
		}

		m_Ambient = 0.0f;
		m_SkyLuminance = 0.0f;
		if (Entity environment = m_Scene.GetEnvironmentEntity())
		{
			const auto& env = environment.GetComponent<EnvironmentComponent>();
			const float ambient = Math::Luminance(env.AmbientColor);
			m_Ambient = ambient * glm::pi<float>() * (env.EnvironmentMap.empty() ? 1.0f : env.EnvironmentIntensity);
			m_SkyLuminance = ambient * env.SkyboxIntensity;
		}
	}

	bool AnalyticLuminanceProvider::IsOccluded(const glm::vec3& point, const LightSample& light, const std::vector<UUID>& ignore) const
	{
		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		if (!physics || !light.CastsShadows)
			return false;

		glm::vec3 direction;
		float distance;
		if (light.Type == LightSample::Kind::Directional)
		{
			direction = -light.Direction;
			distance = DirectionalShadowDistance;
		}
		else
		{
			direction = light.Position - point;
			distance = glm::length(direction);
			if (distance < Math::Epsilon)
				return false;
			direction /= distance;
		}

		auto filter = [&](UUID entity) {
			if (Contains(ignore, entity) || Contains(light.Related, entity))
				return false;
			std::optional<Entity> hit = m_Scene.TryGetEntityByUUID(entity);
			if (hit)
			{
				const RigidBodyComponent* body = hit->TryGetComponent<RigidBodyComponent>();
				if (body && (body->TransparentToSight || body->IsTrigger))
					return false;
			}
			return true;
		};
		const glm::vec3 start = point + direction * ShadowBias;
		return physics->Raycast(start, direction, std::max(distance - ShadowBias * 2.0f, 0.0f), filter).has_value();
	}

	float PointLighting::Facing(const glm::vec3& normal) const
	{
		float total = Ambient;
		for (const IncomingLight& light : Lights)
			total += light.Illuminance * std::max(glm::dot(normal, light.Direction), 0.0f);
		return total;
	}

	float PointLighting::Rounded(const glm::vec3& viewDirection) const
	{
		float total = Ambient;
		for (const IncomingLight& light : Lights)
			total += light.Illuminance * (1.0f + glm::dot(viewDirection, light.Direction)) * 0.5f;
		return total;
	}

	float PointLighting::Average() const
	{
		float total = Ambient;
		for (const IncomingLight& light : Lights)
			total += light.Illuminance * 0.5f;
		return total;
	}

	PointLighting AnalyticLuminanceProvider::GatherLighting(const glm::vec3& point, const std::vector<UUID>& ignore) const
	{
		PointLighting lighting;
		lighting.Ambient = m_Ambient;
		for (const LightSample& light : m_Lights)
		{
			float contribution = 0.0f;
			glm::vec3 toLight;
			if (light.Type == LightSample::Kind::Directional)
			{
				toLight = -light.Direction;
				contribution = light.Intensity;
			}
			else
			{
				toLight = light.Position - point;
				const float distance = glm::length(toLight);
				if (distance >= light.Range || distance < Math::Epsilon)
					continue;
				toLight /= distance;
				contribution = light.Intensity * LightUtils::DistanceAttenuation(distance, light.Range);
				if (light.Type == LightSample::Kind::Spot)
					contribution *= LightUtils::SpotAttenuation(-toLight, light.Direction, light.CosOuter, light.CosInner);
			}
			if (contribution <= 1e-6f || IsOccluded(point, light, ignore))
				continue;
			lighting.Lights.push_back({ toLight, contribution });
		}
		return lighting;
	}

	// Keep in step with CSFogScatter in shaders/Fog.hlsl: the same lamps, falloff and phase; a lamp with a shadow
	// map is shadowed (here by a ray), one without lights the fog at FogUnshadowedLight, the sun or moon in full.
	float AnalyticLuminanceProvider::FogLight(const glm::vec3& point, const glm::vec3& towardEye, float anisotropy) const
	{
		float total = 0.0f;
		const std::vector<UUID> none;
		for (const LightSample& light : m_Lights)
		{
			glm::vec3 toLight;
			float strength = light.Intensity;
			if (light.Type == LightSample::Kind::Directional)
				toLight = -light.Direction;
			else
			{
				toLight = light.Position - point;
				const float distance = glm::length(toLight);
				if (distance >= light.Range || distance < Math::Epsilon)
					continue;
				toLight /= distance;
				strength *= LightUtils::DistanceAttenuation(distance, light.Range);
				if (light.Type == LightSample::Kind::Spot)
					strength *= LightUtils::SpotAttenuation(-toLight, light.Direction, light.CosOuter, light.CosInner);
			}
			if (strength <= 1e-7f)
				continue;
			if (light.RenderShadows)
			{
				if (IsOccluded(point, light, none))
					continue;
			}
			else if (light.Type != LightSample::Kind::Directional)
				strength *= FogUnshadowedLight;
			total += strength * FogPhase(glm::dot(-toLight, towardEye), anisotropy);
		}
		return total;
	}

	float AnalyticLuminanceProvider::Illuminance(const glm::vec3& point, const glm::vec3& normal, const std::vector<UUID>& ignore) const
	{
		const PointLighting lighting = GatherLighting(point, ignore);
		if (glm::dot(normal, normal) < 1e-8f)
		{
			float total = lighting.Ambient;
			for (const IncomingLight& light : lighting.Lights)
				total += light.Illuminance;
			return total;
		}
		return lighting.Facing(glm::normalize(normal));
	}

	SurfaceSample AnalyticLuminanceProvider::GetSurface(UUID entityID) const
	{
		SurfaceSample sample;
		std::optional<Entity> entity = m_Scene.TryGetEntityByUUID(entityID);
		if (!entity)
			return sample;

		if (const MaterialComponent* material = entity->TryGetComponent<MaterialComponent>())
		{
			sample.Albedo = Math::Luminance(glm::vec3(material->BaseColor));
			sample.Emissive = material->EmissiveEnabled ? Math::Luminance(material->EmissiveColor) * material->EmissiveIntensity : 0.0f;
			return sample;
		}
		if (const MeshComponent* meshComponent = entity->TryGetComponent<MeshComponent>())
		{
			if (std::shared_ptr<const MeshData> mesh = AssetManager::GetMesh(meshComponent->Mesh); mesh && !mesh->Materials.empty())
			{
				const MaterialData& material = mesh->Materials.front();
				sample.Albedo = Math::Luminance(glm::vec3(material.BaseColor));
				sample.Emissive = Math::Luminance(material.Emissive);
			}
		}
		return sample;
	}

}
