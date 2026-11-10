#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Math/Math.h"

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Penumbra {

	// ------------------------------------------------------------------------
	// Identity and hierarchy
	// ------------------------------------------------------------------------

	struct IDComponent
	{
		UUID ID;
	};

	struct TagComponent
	{
		std::string Tag;
	};

	// Parent/children links by UUID (stable across serialization and play-mode copies).
	struct RelationshipComponent
	{
		UUID Parent = UUID::Invalid;
		std::vector<UUID> Children;
	};

	// Local transform relative to the parent entity (or world if there is none).
	struct TransformComponent
	{
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };

		glm::mat4 GetTransform() const { return Math::ComposeTransform(Translation, Rotation, Scale); }
		void SetTransform(const glm::mat4& transform) { Math::DecomposeTransform(transform, Translation, Rotation, Scale); }
		glm::vec3 GetEulerDegrees() const { return Math::EulerDegreesFromQuat(Rotation); }
		void SetEulerDegrees(const glm::vec3& degrees) { Rotation = Math::QuatFromEulerDegrees(degrees); }
	};

	// ------------------------------------------------------------------------
	// Rendering
	// ------------------------------------------------------------------------

	struct CameraComponent
	{
		float VerticalFOV = 60.0f; // degrees
		float NearClip = 0.05f;
		float FarClip = 500.0f;
		bool Primary = true;
	};

	// References a mesh asset: a glTF file relative to the project's asset directory,
	// or a built-in primitive ("builtin:cube", "builtin:sphere", "builtin:plane",
	// "builtin:capsule", "builtin:cylinder").
	struct MeshComponent
	{
		std::string Mesh = "builtin:cube";
		bool CastShadows = true;
		bool Visible = true;
	};

	// Overrides every material of the mesh. Without it the mesh's own (glTF) materials are used.
	struct MaterialComponent
	{
		glm::vec4 BaseColor{ 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.6f;
		glm::vec3 EmissiveColor{ 1.0f };
		float EmissiveIntensity = 0.0f;
		// Toggled by extinguish/ignite reactions without losing the authored intensity.
		bool EmissiveEnabled = true;
		std::string AlbedoTexture;
		std::string NormalTexture;
		std::string MetallicRoughnessTexture;
		std::string EmissiveTexture;
		// Alpha blending for translucent surfaces (smoke, glass).
		bool Translucent = false;
	};

	struct DirectionalLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 1.0f;
		bool Enabled = true;
		bool CastShadows = true;
		// World-space light size used for percentage-closer soft shadows.
		float ShadowSoftness = 1.0f;
	};

	struct PointLightComponent
	{
		glm::vec3 Color{ 1.0f, 0.75f, 0.45f };
		float Intensity = 10.0f;
		float Range = 8.0f;
		bool Enabled = true;
		bool CastShadows = false;
		// Animated flicker amplitude (0 = steady), handy for torches.
		float Flicker = 0.0f;
	};

	struct SpotLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 20.0f;
		float Range = 15.0f;
		float InnerAngle = 20.0f; // degrees, half-angle
		float OuterAngle = 30.0f; // degrees, half-angle
		bool Enabled = true;
		bool CastShadows = true;
	};

	// Scene-wide environment and post-processing. One per scene (the first one found wins).
	struct EnvironmentComponent
	{
		std::string EnvironmentMap; // equirectangular .hdr in the asset directory
		float EnvironmentIntensity = 1.0f;
		float SkyboxIntensity = 1.0f;
		glm::vec3 AmbientColor{ 0.02f, 0.02f, 0.03f }; // used when no environment map is set
		float Exposure = 1.0f;
		float FogDensity = 0.0f; // also used as the perception atmosphere coefficient (beta)
		glm::vec3 FogColor{ 0.05f, 0.05f, 0.06f };
		bool SSAOEnabled = true;
		float SSAORadius = 0.5f;
		float SSAOIntensity = 1.0f;
		float BloomIntensity = 0.04f;
	};

	// ------------------------------------------------------------------------
	// Physics
	// ------------------------------------------------------------------------

	enum class RigidBodyType : uint8_t
	{
		Static = 0,
		Dynamic,
		Kinematic
	};

	struct RigidBodyComponent
	{
		RigidBodyType Type = RigidBodyType::Static;
		float Mass = 1.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float Friction = 0.6f;
		float Restitution = 0.0f;
		float GravityFactor = 1.0f;
		bool IsTrigger = false;
		// Continuous collision detection for fast projectiles (arrows).
		bool ContinuousCollision = false;
		// Ignored by perception line-of-sight tests (glass, foliage, smoke...).
		bool TransparentToSight = false;

		// Runtime only
		uint32_t RuntimeBodyID = 0xFFFFFFFF;
	};

	struct BoxColliderComponent
	{
		glm::vec3 HalfExtents{ 0.5f };
		glm::vec3 Offset{ 0.0f };
	};

	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Offset{ 0.0f };
	};

	struct CapsuleColliderComponent
	{
		float Radius = 0.3f;
		float HalfHeight = 0.6f; // half height of the cylindrical part
		glm::vec3 Offset{ 0.0f };
	};

	// Static triangle-mesh collider built from the entity's MeshComponent.
	struct MeshColliderComponent
	{
		bool UseMeshComponent = true;
	};

	struct CharacterControllerComponent
	{
		float Radius = 0.3f;
		float Height = 1.8f; // can change at runtime (crouching); the capsule only grows if there is room
		float MaxSlopeDegrees = 50.0f;
		float StepHeight = 0.35f;
		float GravityFactor = 1.0f;

		// Runtime only
		glm::vec3 RuntimeDesiredVelocity{ 0.0f };
		glm::vec3 RuntimeVelocity{ 0.0f };
		bool RuntimeGrounded = false;
		bool RuntimeJumpRequested = false;
		float RuntimeJumpSpeed = 0.0f;
	};

	// ------------------------------------------------------------------------
	// Scripting
	// ------------------------------------------------------------------------

	using ScriptValue = std::variant<bool, double, std::string, glm::vec3>;

	struct ScriptComponent
	{
		std::string Script; // .lua path relative to the asset directory
		// Per-instance overrides of the script's Properties table.
		std::unordered_map<std::string, ScriptValue> Properties;
	};

	// ------------------------------------------------------------------------
	// Audio
	// ------------------------------------------------------------------------

	struct AudioSourceComponent
	{
		std::string Clip; // .wav / .mp3 / .flac path relative to the asset directory
		float Volume = 1.0f;
		float Pitch = 1.0f;
		bool Loop = false;
		bool PlayOnStart = false;
		bool Spatial = true;
		float MinDistance = 1.0f;
		float MaxDistance = 30.0f;
		// How far (meters) NPCs can hear this sound while it plays. 0 = inaudible to AI.
		float NoiseRadius = 0.0f;
	};

	struct AudioListenerComponent
	{
		bool Active = true;
	};

	// ------------------------------------------------------------------------
	// AI perception (see docs/GAME ENGINE SKRADANKA.md, "ShadowProto")
	// ------------------------------------------------------------------------

	enum class AwarenessState : uint8_t
	{
		Unaware = 0,
		Suspicious,
		Searching,
		Alarmed
	};

	struct PerceptionComponent
	{
		// Vision
		float EyeHeight = 1.65f;
		float ViewDistance = 25.0f;
		float FieldOfView = 120.0f;     // full cone angle, degrees
		float PeripheralFactor = 0.2f;  // P(theta) at the edge of the cone
		float ContrastMin = 0.1f;       // C_min
		float DarkSensitivity = 0.004f; // k_dark: how much darkness of the observed area raises the threshold
		float GlareSensitivity = 0.5f;  // threshold increase per decade the eye is adapted above the observed area
		float LuminanceFloor = 0.005f;  // eps in the contrast formula: noise floor of the eye
		float AdaptBrightTime = 0.5f;   // tau_bright, seconds
		float AdaptDarkTime = 6.0f;     // tau_dark, seconds
		float MotionFactor = 0.3f;      // m_motion
		float ReferenceAngularSize = 0.05f; // radians - target size at which S reaches 1
		float SuspicionRise = 0.6f;     // r_rise
		float SuspicionDecay = 0.08f;   // r_decay
		float SuspiciousThreshold = 0.3f;
		float SearchingThreshold = 0.7f;
		// Hearing: a noise of radius R at distance d is heard when d < R * HearingSensitivity.
		float HearingSensitivity = 1.0f;
		float HearingSuspicion = 0.35f; // suspicion added by a clearly heard noise

		// Runtime only
		float RuntimeAdaptation = 0.05f; // L_a
		float RuntimeSuspicion = 0.0f;   // D
		float RuntimeStimulus = 0.0f;    // last B
		float RuntimeVisibleFraction = 0.0f;
		float RuntimeContrast = 0.0f;
		AwarenessState RuntimeState = AwarenessState::Unaware;
		glm::vec3 RuntimeLastStimulusPosition{ 0.0f };
		bool RuntimeHasStimulus = false;
		UUID RuntimeLastTarget = UUID::Invalid;
	};

	// Something NPCs can see: the player. Silhouette points are local offsets sampled
	// as the "control points" of the silhouette (head, shoulders, sides, centre...).
	struct PerceptionTargetComponent
	{
		std::vector<glm::vec3> SilhouettePoints = {
			{ 0.0f, 1.70f, 0.0f },  // head
			{ -0.22f, 1.45f, 0.0f }, // left shoulder
			{ 0.22f, 1.45f, 0.0f },  // right shoulder
			{ 0.0f, 1.15f, 0.0f },  // chest
			{ -0.2f, 0.9f, 0.0f },  // left hip
			{ 0.2f, 0.9f, 0.0f },   // right hip
			{ 0.0f, 0.45f, 0.0f },  // knees
		};
		float Reflectance = 0.35f; // diffuse albedo of clothing
		float Radius = 0.3f;       // used for angular size

		// Runtime only
		glm::vec3 RuntimeVelocity{ 0.0f };
		glm::vec3 RuntimePreviousPosition{ 0.0f };
		bool RuntimeHasPreviousPosition = false;
		float RuntimeLightLevel = 0.0f;   // average luminance on the silhouette
		float RuntimeExposure = 0.0f;     // max normalized stimulus over all observers (0..1+)
	};

	// Smoke / fog volume that flattens contrast along sight lines passing through it.
	struct SmokeVolumeComponent
	{
		float Radius = 2.5f;
		float Density = 0.8f; // extinction per meter (beta)
		float Lifetime = 0.0f; // seconds, 0 = permanent

		// Runtime only
		float RuntimeAge = 0.0f;
	};

	// ------------------------------------------------------------------------
	// Stim / response ("Act/React") - elemental interactions between objects
	// ------------------------------------------------------------------------

	enum class StimDelivery : uint8_t
	{
		Contact = 0, // delivered to whatever the body touches (projectiles)
		Radius,      // delivered once, to everything within Radius (explosions)
		Continuous   // delivered every simulation tick within Radius (a torch flame)
	};

	struct StimSourceComponent
	{
		std::string Stim = "Fire"; // free-form stim name: Fire, Water, Kinetic, Gas, ...
		float Intensity = 1.0f;
		StimDelivery Delivery = StimDelivery::Contact;
		float Radius = 1.0f;
		bool Enabled = true;
		bool DestroyOnContact = false;
		bool RequireLineOfSight = false;
	};

	enum class StimActionType : uint8_t
	{
		DisableLights = 0, // extinguish: lights + emission + continuous stims on the entity and its children
		EnableLights,      // ignite
		Destroy,
		SpawnPrefab,       // Asset = prefab path, spawned at the entity's position
		EmitStim,          // Asset = stim name, Value = intensity, Radius = radius
		RadialImpulse,     // Value = impulse strength, Radius = radius
		PlaySound,         // Asset = clip path
		EmitNoise,         // Radius = noise radius heard by NPCs
		ScriptEvent        // calls on_stim_event(self, Asset) on the entity's script
	};

	struct StimAction
	{
		StimActionType Type = StimActionType::Destroy;
		std::string Asset;
		float Value = 0.0f;
		float Radius = 0.0f;
	};

	struct StimResponseRule
	{
		std::string Stim = "Fire";
		float Threshold = 0.5f; // minimum received intensity that triggers the rule
		bool Once = true;       // disable the rule after it fired
		std::vector<StimAction> Actions;

		// Runtime only
		bool RuntimeFired = false;
	};

	struct StimResponseComponent
	{
		std::vector<StimResponseRule> Rules;
	};

	// ------------------------------------------------------------------------
	// Prefab bookkeeping
	// ------------------------------------------------------------------------

	struct PrefabComponent
	{
		std::string Prefab; // prefab path this entity was instantiated from
	};

	template<typename... Component>
	struct ComponentGroup
	{
	};

	// Components copied when duplicating entities / entering play mode.
	using AllComponents = ComponentGroup<TransformComponent, RelationshipComponent, CameraComponent, MeshComponent, MaterialComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, EnvironmentComponent, RigidBodyComponent, BoxColliderComponent,
		SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent, CharacterControllerComponent, ScriptComponent,
		AudioSourceComponent, AudioListenerComponent, PerceptionComponent, PerceptionTargetComponent, SmokeVolumeComponent,
		StimSourceComponent, StimResponseComponent, PrefabComponent>;

}
