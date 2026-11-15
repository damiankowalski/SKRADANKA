#pragma once

#include "Penumbra/Animation/AnimationData.h"
#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Math/Math.h"

#include <memory>
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
		// Source-file node names whose geometry is not drawn (e.g. unused weapons of a character).
		std::vector<std::string> HiddenNodes;
	};

	// Plays the skeletal animation clips of the entity's mesh (a glTF with skins/animations).
	// Changing Clip (from the editor, Lua or the automation API) crossfades to the new clip.
	struct AnimatorComponent
	{
		std::string Clip;           // clip to play; empty = rest pose
		float Speed = 1.0f;         // playback rate multiplier
		bool Loop = true;
		bool Playing = true;        // false freezes the current frame
		float BlendDuration = 0.2f; // seconds of crossfade when Clip changes

		// Runtime only
		std::string RuntimeClip;          // clip currently playing (lags Clip by one update)
		float RuntimeTime = 0.0f;         // seconds into RuntimeClip
		std::string RuntimePreviousClip;  // clip being faded out
		float RuntimePreviousTime = 0.0f;
		float RuntimeBlend = 1.0f;        // 0..1 weight of RuntimeClip during a crossfade
		float RuntimeFadeDuration = 0.0f; // duration of the current crossfade
		float RuntimeRequestedFade = -1.0f; // one-shot crossfade override for the next clip change (anim.play)
		bool RuntimeFinished = false;     // a non-looping clip reached its end
		// Not a field (not saved, not in the registry): a pose that replaces the animation while set,
		// indexed like the skeleton. A ragdoll writes its bodies' pose here (RagdollSystem).
		std::shared_ptr<const Pose> RuntimePoseOverride;
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
		// Background noise level (0-10: wind, rain, machinery) that masks quieter sounds for NPCs.
		float AmbientNoise = 0.0f;
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
		// Loudness (0-10, see docs/Perception.md) NPCs hear while it plays. 0 = inaudible to AI.
		float NoiseLoudness = 0.0f;
	};

	struct AudioListenerComponent
	{
		bool Active = true;
	};

	// ------------------------------------------------------------------------
	// AI perception (see docs/GAME ENGINE SKRADANKA.md, "ShadowProto")
	// ------------------------------------------------------------------------

	// What an NPC is doing about the strongest stimulus it is still reacting to. The state
	// follows the remaining reaction strength (0-10) and steps down as the reaction fades.
	enum class AwarenessState : uint8_t
	{
		Unaware = 0,   // patrolling
		Suspicious,    // something faint: stop, turn and look
		Investigating, // walk over and have a look
		Searching,     // search the area around the stimulus
		Alarmed        // the intruder is (or was just) seen: chase, raise the alarm
	};

	enum class StimulusKind : uint8_t
	{
		None = 0,
		Heard,
		Seen,
		Reported // injected by a script (another guard's shout, a scripted event...)
	};

	struct PerceptionComponent
	{
		// Vision
		float EyeHeight = 1.65f;
		float ViewDistance = 25.0f;
		float FieldOfView = 130.0f;     // full cone angle, degrees (focus cone plus the peripheral bands)
		float FocusFieldOfView = 90.0f; // central cone of full contrast; beyond it, out to FieldOfView, the eye only half sees
		float PeripheralContrast = 0.5f; // contrast multiplier for silhouette points in the peripheral bands
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
		float SuspiciousThreshold = 0.3f; // suspicion D from which a sighting is a stimulus of strength 10 * D
		// Hearing: a noise of loudness L (0-10) carries 1.4 * 2^(L/2) metres, losing 2 levels per
		// doubling of distance; HearingSensitivity scales the distances (2 = hears twice as far).
		float HearingSensitivity = 1.0f;
		// Reactions: a stimulus of strength s (0-10) keeps the NPC busy ReactionDuration * (s/10)^2
		// seconds. The remaining strength picks the state: >= AlarmLevel Alarmed, >= SearchLevel
		// Searching, >= InvestigateLevel Investigating, > 0 Suspicious.
		float ReactionDuration = 90.0f;
		float InvestigateLevel = 4.0f;
		float SearchLevel = 7.0f;
		float AlarmLevel = 9.0f;
		// After a search the NPC stays more alert (hears further, notices faster) for this long.
		float AlertMemoryTime = 120.0f;
		// Levels added to what he is reacting to when a distraction (thunder, dogs...) offers an
		// innocent explanation: a diligent guard is harder to talk out of a search.
		float Diligence = 0.0f;
		// Strength of the reaction to finding a body (unconscious or dead NPC) in plain sight.
		float BodyAlarm = 8.0f;
		// Strength of the reaction to an arrow seen flying by (searched for where it came from).
		float ProjectileAlarm = 8.0f;
		// Touch beats shadow. An intruder within TouchRange of his capsule (bumping into him or
		// bumped into) is a stimulus of strength TouchAlarm whatever the light. One closer than
		// CloseRange in his view is made out even in the dark, though not at once: the sight is
		// at least CloseRangeStimulus (scaled by how much of the figure is in view, and how far
		// to the side).
		float TouchRange = 0.15f;
		float TouchAlarm = 10.0f;
		float CloseRange = 1.2f;
		float CloseRangeStimulus = 2.0f;

		// Runtime only
		float RuntimeAdaptation = 0.05f; // L_a
		float RuntimeSuspicion = 0.0f;   // D
		float RuntimeStimulus = 0.0f;    // last B
		float RuntimeVisibleFraction = 0.0f;
		float RuntimeContrast = 0.0f;
		AwarenessState RuntimeState = AwarenessState::Unaware;
		float RuntimeReactionStrength = 0.0f; // remaining strength (0-10) of the current reaction
		float RuntimeReactionTime = 0.0f;     // seconds left
		StimulusKind RuntimeStimulusKind = StimulusKind::None;
		std::string RuntimeStimulusCause;     // noise name ("Footstep", "Impact"...), "Seen" or a script's label
		float RuntimeAlertness = 0.0f;        // 0-1 memory of recent searches
		glm::vec3 RuntimeLastStimulusPosition{ 0.0f };
		bool RuntimeHasStimulus = false;
		UUID RuntimeLastTarget = UUID::Invalid;
	};

	// What a floor or an object is made of, for the noises made on it and with it. Footsteps
	// (scripted, see Scripts/AI/Noises.lua) and impacts add NoiseModifier of the surface they
	// land on; dynamic objects with an ImpactLoudness make noise when they hit something.
	struct SurfaceComponent
	{
		std::string Material = "Stone"; // reported with the noise: "Footstep.Metal", "Impact.Wood"...
		float NoiseModifier = 0.0f;     // loudness levels added on this surface (carpet -2, moss -3, metal grate +2.5)
		float ImpactLoudness = 0.0f;    // loudness (0-10) of this object hitting a neutral floor at 4 m/s; 0 = silent
		std::string ImpactSound;        // optional clip played on impacts
		// Levels a sound loses passing through this object: a door (2-3) muffles less than a wall.
		// Negative: it blocks sound like any wall (PerceptionModel::WallLoss).
		float SoundLoss = -1.0f;

		// Runtime only
		float RuntimeLastImpactTime = -1.0f;
	};

	enum class TargetKind : uint8_t
	{
		Intruder = 0, // the player: raises suspicion
		Body,         // an unconscious or dead NPC: noticed once, then searched for whoever did it
		Projectile    // an arrow in flight: judged every frame, noticed once, then traced back to where it came from
	};

	// Whether a character is up. Unconscious or dead NPCs neither see nor hear, lie where they fell
	// and become bodies other NPCs can notice (ai.knock_out, the Knockout stim action).
	enum class VitalState : uint8_t
	{
		Awake = 0,
		Unconscious,
		Dead
	};

	struct VitalityComponent
	{
		VitalState State = VitalState::Awake;
		float Health = 100.0f; // the Damage stim action and ai.damage take it away; at 0 the NPC is Dead
	};

	struct RagdollRig; // RagdollSystem's per-body state

	// A character whose body goes limp: knocked out or killed, its skinned model (the first child
	// with an Animator) is handed to jointed physics bodies built from the skeleton (Animation/RagdollSystem).
	struct RagdollComponent
	{
		bool Enabled = true;      // a knockout or death starts the ragdoll
		float Mass = 70.0f;       // kg, the whole body
		float RadiusScale = 1.0f; // limb thickness relative to the built-in humanoid
		float Friction = 0.6f;    // of the body against floors (cloth and leather on stone): below tan(slope) it slides

		// Runtime only
		bool RuntimeActive = false; // physics drives the pose
		bool RuntimeAwake = false;  // still moving; false once the body has come to rest
		std::shared_ptr<RagdollRig> RuntimeRig; // not a field
	};

	// Something NPCs can see: the player, or a body. Silhouette points are local offsets sampled
	// as the "control points" of the silhouette (head, shoulders, sides, centre...).
	struct PerceptionTargetComponent
	{
		TargetKind Kind = TargetKind::Intruder;
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
		float Emissive = 0.0f;     // own luminance (a burning arrow glows in the dark)

		// Runtime only
		glm::vec3 RuntimeVelocity{ 0.0f };
		glm::vec3 RuntimePreviousPosition{ 0.0f };
		bool RuntimeHasPreviousPosition = false;
		float RuntimeLightLevel = 0.0f;   // luminance of the body from the light it stands in (independent of observers)
		float RuntimeLightGem = 0.0f;     // LightLevel mapped to 0..1 for the HUD "light gem"
		float RuntimeExposure = 0.0f;     // max stimulus B over all observers (>= 1: being noticed)
		glm::vec3 RuntimeOrigin{ 0.0f };  // where it was first seen by the engine (an arrow: where it was shot from)
	};

	// Procedural animated fire (torches, campfires) rendered as layered billboards in the HDR
	// pass. Pair it with a flickering PointLight; extinguishing (DisableLights) turns both off.
	struct FlameComponent
	{
		float Height = 0.5f;     // metres, from the entity origin upwards
		float Radius = 0.15f;    // half width at the base, metres
		float Intensity = 6.0f;  // HDR brightness of the core
		float Speed = 1.0f;      // animation speed multiplier
		glm::vec3 Color{ 1.0f, 0.45f, 0.12f }; // body colour; the core blends towards white-yellow
		bool Enabled = true;
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
		EmitNoise,         // Value = loudness (0-10) heard by NPCs, Asset = optional cause name
		ScriptEvent,       // calls on_stim_event(self, Asset) on the entity's script
		Knockout,          // knocks the NPC out (Asset "Dead" kills it): see ai.knock_out
		Damage,            // Value = health taken from the NPC (at 0 it dies)
		Alarm              // the NPC was hit: a stimulus of strength Value (default 9.5) where the stim came from, Asset = cause
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
	// Navigation
	// ------------------------------------------------------------------------

	// Navmesh build settings for the scene (put it on one entity, e.g. the level root). The
	// navmesh is baked from every static, solid collider when the game starts and rebuilt when
	// one of them is destroyed. Scenes without this component bake with the defaults on first use.
	struct NavMeshComponent
	{
		float CellSize = 0.15f;      // horizontal voxel size, metres
		float CellHeight = 0.1f;     // vertical voxel size, metres
		float AgentRadius = 0.35f;   // walls are kept this far from paths
		float AgentHeight = 1.8f;    // minimum clearance
		float AgentMaxClimb = 0.4f;  // highest step an agent walks up
		float AgentMaxSlope = 45.0f; // degrees

		// Runtime only
		uint32_t RuntimePolygons = 0;
		float RuntimeBuildMilliseconds = 0.0f;
	};

	enum class NavStatus : uint8_t
	{
		Idle = 0, // not moving under navigation control
		Moving,   // following a path
		Arrived,  // reached the destination (or the closest reachable point)
		Failed,   // no path: the agent or the destination is off the navmesh
		Stuck     // blocked for a while; the script decides what to do
	};

	// Lets the navigation system walk a character (CharacterController) along navmesh paths:
	// nav.move_to(entity, position) from Lua. Added automatically by nav.move_to when missing.
	struct NavAgentComponent
	{
		float Speed = 1.4f;             // metres per second
		float TurnSpeed = 540.0f;       // degrees per second when turning to face the path
		float StoppingDistance = 0.3f;  // arrival tolerance, metres
		bool FaceMovement = true;       // rotate the entity towards the direction of travel

		// Runtime only
		NavStatus RuntimeStatus = NavStatus::Idle;
		glm::vec3 RuntimeDestination{ 0.0f };
		float RuntimeRemainingDistance = 0.0f;
		bool RuntimePartialPath = false; // the destination is unreachable; walking to the closest point
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
	using AllComponents = ComponentGroup<TransformComponent, RelationshipComponent, CameraComponent, MeshComponent, AnimatorComponent, MaterialComponent,
		DirectionalLightComponent, PointLightComponent, SpotLightComponent, EnvironmentComponent, RigidBodyComponent, BoxColliderComponent,
		SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent, CharacterControllerComponent, ScriptComponent,
		AudioSourceComponent, AudioListenerComponent, PerceptionComponent, PerceptionTargetComponent, VitalityComponent, SmokeVolumeComponent, FlameComponent,
		StimSourceComponent, StimResponseComponent, NavMeshComponent, NavAgentComponent, SurfaceComponent, PrefabComponent, RagdollComponent>;

}
