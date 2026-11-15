#pragma once

#include "Penumbra/Scene/Components.h"

// Pure functions implementing the silhouette-contrast perception model
// (docs/GAME ENGINE SKRADANKA.md, section 4). No scene or physics access here,
// which keeps every formula independently unit-testable.

namespace Penumbra::PerceptionModel {

	constexpr float Epsilon = 1e-3f;

	// 4.2: Weber-style relative contrast between background and target luminance.
	// C = |Lb - Lp| / max(Lb, Lp, eps), in [0, 1]. eps is the eye's noise floor: when both
	// values are below it (near-total darkness) contrast collapses towards zero.
	float WeberContrast(float backgroundLuminance, float targetLuminance, float floor = Epsilon);

	// 4.3: eye adaptation. Fast towards bright (tauBright), slow towards dark (tauDark).
	float Adapt(float adaptation, float environmentLuminance, float dt, float tauBright, float tauDark);

	// 4.3: contrast threshold needed to notice anything.
	//
	//   C_t = C_min + k_dark / (L_obs + eps) + k_glare * max(0, log10(L_a / (L_obs + eps)))
	//
	// L_obs is the luminance of the observed area (max of target and background). The spec's
	// starting point C_min + k_dark / L_a lowers the threshold when the eye is bright-adapted,
	// which contradicts its own scenarios (a guard stepping from a bright hall into a dark
	// corridor must be briefly blind; a guard with a lantern must see worse outside the beam).
	// Basing the darkness term on the observed area and adding a glare term for an eye adapted
	// brighter than what it looks at reproduces all of them; see docs/Perception.md.
	float ContrastThreshold(float adaptation, float observedLuminance, float contrastMin, float darkSensitivity, float glareSensitivity);

	// 4.4: size factor S = min(1, angularSize / referenceSize).
	float SizeFactor(float targetRadius, float distance, float referenceAngularSize);

	// 4.4: atmospheric attenuation of contrast for a given optical depth (beta * distance,
	// plus the integrated density of smoke volumes along the sight line).
	float AtmosphereFactor(float opticalDepth);

	// 4.5: peripheral sensitivity P(theta): 1 at the centre of the cone falling to edgeFactor
	// at its edge. normalizedAngle = theta / halfFov in [0, 1].
	float PeripheralFactor(float normalizedAngle, float edgeFactor);

	// Two zones of the cone: a silhouette point within the focus cone (angle from the gaze up to
	// focusHalfAngle, radians) keeps its contrast; out in the peripheral bands it is multiplied by
	// peripheralContrast (dimmer, never zero: the thief is harder, not impossible, to spot there).
	float ZoneContrastFactor(float angle, float focusHalfAngle, float peripheralContrast);

	// 4.5: motion factor M = 1 + m * speed * (1 + peripherality), capped at MaxMotionFactor (a 30 m/s arrow
	// is not 20 times as noticeable as a still figure).
	constexpr float MaxMotionFactor = 4.0f;
	float MotionFactor(float speed, float peripherality, float motionWeight);

	// 4.6: combined stimulus B = (C_eff / C_t) * f * S * P * M.
	float Stimulus(float effectiveContrast, float contrastThreshold, float visibleFraction, float sizeFactor, float peripheral, float motion);

	// 4.6: suspicion counter D in [0, 1].
	float UpdateSuspicion(float suspicion, float stimulus, float dt, float riseRate, float decayRate);

	// --- Hearing (loudness on a 0-10 designer scale, docs/Perception.md) ----------------------

	constexpr float NoiseReferenceDistance = 1.4f; // a noise is heard at its full loudness this close
	constexpr float LevelsPerDoubling = 2.0f;      // loudness lost per doubling of distance
	constexpr float WallLoss = 4.0f;               // levels lost going straight through a wall
	constexpr float CornerLoss = 1.0f;             // levels lost travelling around corners (navmesh path)

	// How far a noise of the given loudness carries: 1.4 * 2^(L/2) m (L3 ~ 4 m, L6 ~ 11 m, L9 ~ 32 m).
	float NoiseRange(float loudness);
	// Loudness left after travelling the distance (<= 0: inaudible).
	float HeardLevel(float loudness, float distance);

	constexpr float ReferenceImpactSpeed = 4.0f; // m/s (a drop from about 0.8 m)
	constexpr float MinimumImpactSpeed = 1.0f;   // slower contacts (resting, sliding) are silent

	// Loudness of an object hitting something: its ImpactLoudness at the reference speed, two
	// levels more per doubling of speed, plus the modifier of the surface it hits. 0 below the
	// minimum speed.
	float ImpactLoudness(float baseLoudness, float speed, float surfaceModifier);

	// --- Reactions --------------------------------------------------------------------------

	// Seconds a stimulus of strength s (0-10) keeps an NPC busy: fullDuration * (s/10)^2.
	float ReactionDuration(float strength, float fullDuration);
	// Inverse: the strength still "felt" with timeLeft seconds of reaction remaining.
	float ReactionStrength(float timeLeft, float fullDuration);
	AwarenessState StateFromStrength(float strength, float investigateLevel, float searchLevel, float alarmLevel);

	// A distraction of the given strength (thunder, dogs, a storm) offers an innocent explanation
	// for what an NPC is reacting to. It wins when it is stronger than the remaining reaction plus
	// the NPC's diligence; roll in [-1, 1] times spread adds luck either way.
	bool DistractionWins(float distraction, float reaction, float diligence, float spread, float roll);

	// Maps the luminance of the player's body to the 0..1 HUD light gem on a logarithmic scale
	// (the eye's response): ~0.15 in near darkness, ~0.5 in dim torchlight, 1 in strong light.
	float LightGem(float luminance);

	// Diffuse reflected luminance of a surface (Lambert) plus its own emission.
	float SurfaceLuminance(float albedo, float illuminance, float emissive);

}
