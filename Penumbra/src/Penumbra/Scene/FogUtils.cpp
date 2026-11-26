#include "pnpch.h"
#include "Penumbra/Scene/FogUtils.h"

#include "Penumbra/Math/Math.h"
#include "Penumbra/Scene/Components.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scene/Scene.h"

#include <algorithm>
#include <limits>

namespace Penumbra {

	namespace {

		// Antiderivative of the height profile (1 below Height, exp(-falloff * (y - Height)) above).
		float HeightProfileIntegral(float y, float height, float falloff)
		{
			if (y <= height)
				return y;
			return height + (1.0f - std::exp(-falloff * (y - height))) / falloff;
		}

	}

	// Keep in step with FogPhase in shaders/Fog.hlsli.
	float FogPhase(float cosTheta, float anisotropy)
	{
		const float g2 = anisotropy * anisotropy;
		return (1.0f - g2) / std::pow(std::max(1.0f + g2 - 2.0f * anisotropy * cosTheta, 1e-4f), 1.5f);
	}

	// Keep in step with FogBoxWeight in shaders/Fog.hlsli.
	float FogBox::Weight(const glm::vec3& point) const
	{
		const glm::vec3 local = glm::abs(glm::inverse(Rotation) * (point - Center));
		const glm::vec3 inside = HalfExtents - local;
		const float depth = std::min(inside.x, std::min(inside.y, inside.z));
		if (depth <= 0.0f)
			return 0.0f;
		return EdgeFade <= 0.0f ? 1.0f : std::clamp(depth / EdgeFade, 0.0f, 1.0f);
	}

	bool FogBox::Clip(const glm::vec3& from, const glm::vec3& direction, float length, float& enter, float& exit) const
	{
		const glm::quat toLocal = glm::inverse(Rotation);
		const glm::vec3 localOrigin = toLocal * (from - Center);
		const glm::vec3 localDirection = toLocal * direction;
		enter = 0.0f;
		exit = length;
		for (int axis = 0; axis < 3; axis++)
		{
			if (std::abs(localDirection[axis]) < 1e-8f)
			{
				if (std::abs(localOrigin[axis]) > HalfExtents[axis])
					return false;
				continue;
			}
			float t1 = (-HalfExtents[axis] - localOrigin[axis]) / localDirection[axis];
			float t2 = (HalfExtents[axis] - localOrigin[axis]) / localDirection[axis];
			if (t1 > t2)
				std::swap(t1, t2);
			enter = std::max(enter, t1);
			exit = std::min(exit, t2);
			if (enter >= exit)
				return false;
		}
		return true;
	}

	FogMedium FogMedium::FromScene(Scene& scene)
	{
		FogMedium medium;
		if (Entity environment = scene.GetEnvironmentEntity())
		{
			const EnvironmentComponent& component = environment.GetComponent<EnvironmentComponent>();
			medium.Density = std::max(component.FogDensity, 0.0f);
			medium.Height = component.FogHeight;
			medium.HeightFalloff = std::max(component.FogHeightFalloff, 0.0f);
			medium.ColorLuminance = Math::Luminance(glm::max(component.FogColor, glm::vec3(0.0f)));
			medium.Scattering = std::max(component.FogScattering, 0.0f);
			medium.Anisotropy = std::clamp(component.FogAnisotropy, -0.9f, 0.9f);
			medium.Volumetric = component.VolumetricFog;
		}
		// Without volumetric fog the renderer applies only the environment's fog: so does everyone else.
		if (!medium.Volumetric)
			return medium;
		for (auto [handle, volume] : scene.GetRegistry().view<FogVolumeComponent>().each())
		{
			const bool clear = volume.Mode == FogVolumeMode::Clear;
			if (!volume.Enabled || (!clear && volume.Density <= 0.0f))
				continue;
			Entity entity(handle, &scene);
			FogBox box;
			box.Center = scene.GetWorldPosition(entity);
			box.Rotation = glm::normalize(scene.GetWorldRotation(entity));
			box.HalfExtents = glm::max(volume.HalfExtents, glm::vec3(0.0f));
			box.Density = volume.Density;
			box.EdgeFade = std::max(volume.EdgeFade, 0.0f);
			if (clear)
			{
				box.Density = 0.0f;
				medium.Clears.push_back(box);
			}
			else
				medium.Boxes.push_back(box);
		}
		return medium;
	}

	// Keep in step with FogGlobalDensity in shaders/Fog.hlsli.
	float FogMedium::GlobalDensityAt(float y) const
	{
		if (HeightFalloff <= 0.0f || y <= Height)
			return Density;
		return Density * std::exp(-HeightFalloff * (y - Height));
	}

	float FogMedium::ClearAt(const glm::vec3& point) const
	{
		float clear = 0.0f;
		for (const FogBox& box : Clears)
			clear = std::max(clear, box.Weight(point));
		return clear;
	}

	// Keep in step with FogDensityAt in shaders/Fog.hlsl.
	float FogMedium::DensityAt(const glm::vec3& point) const
	{
		float density = GlobalDensityAt(point.y) * (1.0f - ClearAt(point));
		for (const FogBox& box : Boxes)
			density += box.Density * box.Weight(point);
		return density;
	}

	float FogMedium::OpticalDepth(const glm::vec3& from, const glm::vec3& to) const
	{
		const glm::vec3 delta = to - from;
		const float length = glm::length(delta);
		if (length < 1e-6f)
			return 0.0f;

		// The environment's fog: the height profile averaged over the segment's vertical span.
		float depth = Density * length;
		const float rise = delta.y;
		if (HeightFalloff > 0.0f && Density > 0.0f)
		{
			if (std::abs(rise) < 1e-4f)
				depth = GlobalDensityAt(from.y) * length;
			else
				depth = Density * length *
						(HeightProfileIntegral(to.y, Height, HeightFalloff) - HeightProfileIntegral(from.y, Height, HeightFalloff)) / rise;
		}

		// What the Clear volumes keep out: the environment's density times ClearAt, sampled over the union of the
		// parts of the segment inside them (overlapping rooms are not taken away twice).
		const glm::vec3 direction = delta / length;
		if (Density > 0.0f && !Clears.empty())
		{
			std::vector<std::pair<float, float>> spans;
			for (const FogBox& box : Clears)
			{
				float enter = 0.0f, exit = 0.0f;
				if (box.Clip(from, direction, length, enter, exit))
					spans.emplace_back(enter, exit);
			}
			std::sort(spans.begin(), spans.end());
			for (size_t i = 0; i < spans.size();)
			{
				const float enter = spans[i].first;
				float exit = spans[i].second;
				for (i++; i < spans.size() && spans[i].first <= exit; i++)
					exit = std::max(exit, spans[i].second);
				const int samples = std::max(BoxSamples, static_cast<int>(std::ceil((exit - enter) / ClearStep)));
				const float step = (exit - enter) / static_cast<float>(samples);
				for (int s = 0; s < samples; s++)
				{
					const glm::vec3 point = from + direction * (enter + step * (static_cast<float>(s) + 0.5f));
					depth -= GlobalDensityAt(point.y) * ClearAt(point) * step;
				}
			}
			depth = std::max(depth, 0.0f);
		}

		// Boxes: clip the segment to each one, then sample the weight along the part inside.
		for (const FogBox& box : Boxes)
		{
			float enter = 0.0f, exit = 0.0f;
			if (!box.Clip(from, direction, length, enter, exit))
				continue;
			const float step = (exit - enter) / static_cast<float>(BoxSamples);
			float weights = 0.0f;
			for (int i = 0; i < BoxSamples; i++)
				weights += box.Weight(from + direction * (enter + step * (static_cast<float>(i) + 0.5f)));
			depth += box.Density * weights * step;
		}
		return depth;
	}

	FogPath FogMedium::Integrate(const glm::vec3& from, const glm::vec3& to, const std::function<float(const glm::vec3&)>& lampLight) const
	{
		FogPath path;
		const glm::vec3 delta = to - from;
		const float length = glm::length(delta);
		if (length < 1e-6f || IsEmpty())
			return path;
		const int steps = std::clamp(static_cast<int>(std::ceil(length / MaxScatterStep)), MinScatterSteps, MaxScatterSteps);
		const float step = length / static_cast<float>(steps);
		for (int i = 0; i < steps && path.Transmittance > MinScatterTransmittance; i++)
		{
			const glm::vec3 point = from + delta * ((static_cast<float>(i) + 0.5f) / static_cast<float>(steps));
			const float density = DensityAt(point);
			if (density <= 0.0f)
				continue;
			float source = ColorLuminance;
			if (Volumetric && Scattering > 0.0f && lampLight)
				source += Scattering * lampLight(point);
			const float stepTransmittance = std::exp(-density * step);
			path.InScattered += path.Transmittance * source * (1.0f - stepTransmittance);
			path.Transmittance *= stepTransmittance;
		}
		return path;
	}

}
