#include "pnpch.h"
#include "Penumbra/AI/PerceptionModel.h"

#include <glm/gtc/constants.hpp>

#include <array>

namespace Penumbra::PerceptionModel {

	float WeberContrast(float backgroundLuminance, float targetLuminance, float floor)
	{
		const float lb = std::max(backgroundLuminance, 0.0f);
		const float lp = std::max(targetLuminance, 0.0f);
		return std::abs(lb - lp) / std::max({ lb, lp, floor, Epsilon });
	}

	float Adapt(float adaptation, float environmentLuminance, float dt, float tauBright, float tauDark)
	{
		const float tau = std::max(environmentLuminance > adaptation ? tauBright : tauDark, 1e-4f);
		return adaptation + (environmentLuminance - adaptation) * (1.0f - std::exp(-std::max(dt, 0.0f) / tau));
	}

	float ContrastThreshold(float adaptation, float observedLuminance, float contrastMin, float darkSensitivity, float glareSensitivity)
	{
		const float observed = std::max(observedLuminance, 0.0f) + Epsilon;
		const float darkness = darkSensitivity / observed;
		const float glare = glareSensitivity * std::max(0.0f, std::log10(std::max(adaptation, 0.0f) / observed));
		return contrastMin + darkness + glare;
	}

	float SizeFactor(float targetRadius, float distance, float referenceAngularSize)
	{
		if (distance <= Epsilon)
			return 1.0f;
		const float angularSize = 2.0f * std::atan(targetRadius / distance);
		return std::min(1.0f, angularSize / std::max(referenceAngularSize, Epsilon));
	}

	namespace {

		// SphereShadingHalves tabulated over cosLightToEye in [-1, 1] from the exact integral over the visible disc
		// (a fine midpoint grid; built once, deterministic).
		constexpr int ShadingTableSize = 65;

		std::array<ShadingHalves, ShadingTableSize> BuildShadingTable()
		{
			std::array<ShadingHalves, ShadingTableSize> table{};
			constexpr int Grid = 256;
			for (int entry = 0; entry < ShadingTableSize; entry++)
			{
				const float c = -1.0f + 2.0f * static_cast<float>(entry) / static_cast<float>(ShadingTableSize - 1);
				const float s = std::sqrt(std::max(1.0f - c * c, 0.0f));
				// On the disc: x towards the light's projection, z = depth towards the eye; n.l = c z + s x.
				double lit = 0.0, shade = 0.0;
				int litCount = 0, shadeCount = 0;
				for (int i = 0; i < Grid; i++)
				{
					const float x = -1.0f + 2.0f * (static_cast<float>(i) + 0.5f) / Grid;
					for (int j = 0; j < Grid; j++)
					{
						const float y = -1.0f + 2.0f * (static_cast<float>(j) + 0.5f) / Grid;
						const float r2 = x * x + y * y;
						if (r2 > 1.0f)
							continue;
						const float brightness = std::max(c * std::sqrt(1.0f - r2) + s * x, 0.0f);
						if (x > 0.0f)
						{
							lit += brightness;
							litCount++;
						}
						else
						{
							shade += brightness;
							shadeCount++;
						}
					}
				}
				table[static_cast<size_t>(entry)] = { static_cast<float>(lit / std::max(litCount, 1)), static_cast<float>(shade / std::max(shadeCount, 1)) };
			}
			return table;
		}

	}

	ShadingHalves SphereShadingHalves(float cosLightToEye)
	{
		static const std::array<ShadingHalves, ShadingTableSize> table = BuildShadingTable();
		const float position = (std::clamp(cosLightToEye, -1.0f, 1.0f) + 1.0f) * 0.5f * static_cast<float>(ShadingTableSize - 1);
		const int index = std::min(static_cast<int>(position), ShadingTableSize - 2);
		const float t = position - static_cast<float>(index);
		const ShadingHalves& a = table[static_cast<size_t>(index)];
		const ShadingHalves& b = table[static_cast<size_t>(index) + 1];
		return { a.Lit + (b.Lit - a.Lit) * t, a.Shade + (b.Shade - a.Shade) * t };
	}

	float SelfShadingContrast(float bright, float dark, float floor)
	{
		const float hi = std::max(bright, 0.0f);
		const float lo = std::clamp(dark, 0.0f, hi);
		return (hi - lo) / std::max({ hi, floor, Epsilon });
	}

	float DetailFactor(float targetRadius, float distance, float referenceAngularSize)
	{
		return SizeFactor(targetRadius, distance, referenceAngularSize * SelfShadingDetail);
	}

	float AtmosphereFactor(float opticalDepth) { return std::exp(-std::max(opticalDepth, 0.0f)); }

	float PeripheralFactor(float normalizedAngle, float edgeFactor)
	{
		const float t = std::clamp(normalizedAngle, 0.0f, 1.0f);
		return 1.0f - (1.0f - edgeFactor) * t * t;
	}

	float ZoneContrastFactor(float angle, float focusHalfAngle, float peripheralContrast)
	{
		return angle <= focusHalfAngle ? 1.0f : std::clamp(peripheralContrast, 0.0f, 1.0f);
	}

	float MotionFactor(float speed, float peripherality, float motionWeight)
	{
		return std::min(1.0f + motionWeight * std::max(speed, 0.0f) * (1.0f + std::clamp(peripherality, 0.0f, 1.0f)), MaxMotionFactor);
	}

	float Stimulus(float effectiveContrast, float contrastThreshold, float visibleFraction, float sizeFactor, float peripheral, float motion)
	{
		if (visibleFraction <= 0.0f)
			return 0.0f;
		return (effectiveContrast / std::max(contrastThreshold, Epsilon)) * visibleFraction * sizeFactor * peripheral * motion;
	}

	float UpdateSuspicion(float suspicion, float stimulus, float dt, float riseRate, float decayRate)
	{
		if (stimulus >= 1.0f)
			suspicion += riseRate * (stimulus - 1.0f) * dt;
		else
			suspicion -= decayRate * dt;
		return std::clamp(suspicion, 0.0f, 1.0f);
	}

	float NoiseRange(float loudness)
	{
		if (loudness <= 0.0f)
			return 0.0f;
		return NoiseReferenceDistance * std::exp2(loudness / LevelsPerDoubling);
	}

	float HeardLevel(float loudness, float distance)
	{
		if (loudness <= 0.0f)
			return 0.0f;
		return loudness - LevelsPerDoubling * std::log2(std::max(distance, NoiseReferenceDistance) / NoiseReferenceDistance);
	}

	float ImpactLoudness(float baseLoudness, float speed, float surfaceModifier)
	{
		if (baseLoudness <= 0.0f || speed < MinimumImpactSpeed)
			return 0.0f;
		const float loudness = baseLoudness + LevelsPerDoubling * std::log2(speed / ReferenceImpactSpeed) + surfaceModifier;
		return std::clamp(loudness, 0.0f, 10.0f);
	}

	float ReactionDuration(float strength, float fullDuration)
	{
		const float s = std::clamp(strength, 0.0f, 10.0f) / 10.0f;
		return std::max(fullDuration, 0.0f) * s * s;
	}

	float ReactionStrength(float timeLeft, float fullDuration)
	{
		if (timeLeft <= 0.0f || fullDuration <= 0.0f)
			return 0.0f;
		return std::min(10.0f * std::sqrt(timeLeft / fullDuration), 10.0f);
	}

	AwarenessState StateFromStrength(float strength, float investigateLevel, float searchLevel, float alarmLevel)
	{
		if (strength <= 0.0f)
			return AwarenessState::Unaware;
		if (strength >= alarmLevel)
			return AwarenessState::Alarmed;
		if (strength >= searchLevel)
			return AwarenessState::Searching;
		if (strength >= investigateLevel)
			return AwarenessState::Investigating;
		return AwarenessState::Suspicious;
	}

	bool DistractionWins(float distraction, float reaction, float diligence, float spread, float roll)
	{
		if (distraction <= 0.0f || reaction <= 0.0f)
			return false;
		return distraction + std::max(spread, 0.0f) * std::clamp(roll, -1.0f, 1.0f) > reaction + diligence;
	}

	float LightGem(float luminance)
	{
		constexpr float Dark = 0.002f;  // below: pitch black
		constexpr float Bright = 0.6f;  // above: fully lit
		const float value = std::log2(std::max(luminance, 1e-6f) / Dark) / std::log2(Bright / Dark);
		return std::clamp(value, 0.0f, 1.0f);
	}

	float SurfaceLuminance(float albedo, float illuminance, float emissive)
	{
		return std::max(albedo, 0.0f) * std::max(illuminance, 0.0f) / glm::pi<float>() + std::max(emissive, 0.0f);
	}

}
