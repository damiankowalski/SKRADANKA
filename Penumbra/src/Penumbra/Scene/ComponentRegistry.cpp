#include "pnpch.h"
#include "Penumbra/Scene/ComponentRegistry.h"

#include "Penumbra/Scene/JsonSerialization.h"

namespace Penumbra {

	using json = nlohmann::json;

	namespace {

		enum class FieldKind
		{
			Persistent,
			Runtime
		};

		// What a field's third argument may say: runtime-only, and/or its unit for the editor ("m", "deg", "s").
		struct FieldMeta
		{
			FieldKind Kind = FieldKind::Persistent;
			const char* Unit = nullptr;

			constexpr FieldMeta() = default;
			constexpr FieldMeta(FieldKind kind) : Kind(kind) {}
			constexpr FieldMeta(const char* unit) : Unit(unit) {}
		};

		// v("Sight", Group) starts a group of the fields that follow, for the editor; not a field.
		struct FieldGroup
		{
		};
		constexpr FieldGroup Group;

		template<typename F> constexpr bool IsGroup = std::is_same_v<std::remove_cvref_t<F>, FieldGroup>;

		// ------------------------------------------------------------------------
		// Field lists - one place per component describing its serializable fields.
		// ------------------------------------------------------------------------

		template<typename V> void VisitFields(TagComponent& c, V&& v) { v("Tag", c.Tag); }

		template<typename V>
		void VisitFields(TransformComponent& c, V&& v)
		{
			v("Translation", c.Translation, "m");
			v("Rotation", c.Rotation, "deg");
			v("Scale", c.Scale);
		}

		template<typename V>
		void VisitFields(CameraComponent& c, V&& v)
		{
			v("VerticalFOV", c.VerticalFOV, "deg");
			v("NearClip", c.NearClip, "m");
			v("FarClip", c.FarClip, "m");
			v("Primary", c.Primary);
		}

		template<typename V>
		void VisitFields(MeshComponent& c, V&& v)
		{
			v("Mesh", c.Mesh);
			v("CastShadows", c.CastShadows);
			v("Visible", c.Visible);
			v("HiddenNodes", c.HiddenNodes);
		}

		template<typename V>
		void VisitFields(AnimatorComponent& c, V&& v)
		{
			v("Clip", c.Clip);
			v("Speed", c.Speed);
			v("Loop", c.Loop);
			v("Playing", c.Playing);
			v("BlendDuration", c.BlendDuration);
			v("CurrentClip", c.RuntimeClip, FieldKind::Runtime);
			v("Time", c.RuntimeTime, FieldKind::Runtime);
			v("Blend", c.RuntimeBlend, FieldKind::Runtime);
			v("Finished", c.RuntimeFinished, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(MaterialComponent& c, V&& v)
		{
			v("Surface", Group);
			v("BaseColor", c.BaseColor);
			v("Metallic", c.Metallic);
			v("Roughness", c.Roughness);
			v("Glow", Group);
			v("EmissiveColor", c.EmissiveColor);
			v("EmissiveIntensity", c.EmissiveIntensity);
			v("EmissiveEnabled", c.EmissiveEnabled);
			v("Textures", Group);
			v("AlbedoTexture", c.AlbedoTexture);
			v("NormalTexture", c.NormalTexture);
			v("MetallicRoughnessTexture", c.MetallicRoughnessTexture);
			v("EmissiveTexture", c.EmissiveTexture);
			v("UVScale", c.UVScale);
			v("UVOffset", c.UVOffset);
			v("Blending", Group);
			v("Translucent", c.Translucent);
		}

		template<typename V>
		void VisitFields(DirectionalLightComponent& c, V&& v)
		{
			v("Color", c.Color);
			v("Intensity", c.Intensity);
			v("Enabled", c.Enabled);
			v("CastShadows", c.CastShadows);
			v("ShadowSoftness", c.ShadowSoftness);
		}

		template<typename V>
		void VisitFields(PointLightComponent& c, V&& v)
		{
			v("Color", c.Color);
			v("Intensity", c.Intensity);
			v("Range", c.Range, "m");
			v("Enabled", c.Enabled);
			v("CastShadows", c.CastShadows);
			v("Flicker", c.Flicker);
		}

		template<typename V>
		void VisitFields(SpotLightComponent& c, V&& v)
		{
			v("Color", c.Color);
			v("Intensity", c.Intensity);
			v("Range", c.Range, "m");
			v("InnerAngle", c.InnerAngle, "deg");
			v("OuterAngle", c.OuterAngle, "deg");
			v("Enabled", c.Enabled);
			v("CastShadows", c.CastShadows);
		}

		template<typename V>
		void VisitFields(EnvironmentComponent& c, V&& v)
		{
			v("Sky and light", Group);
			v("EnvironmentMap", c.EnvironmentMap);
			v("EnvironmentIntensity", c.EnvironmentIntensity);
			v("SkyboxIntensity", c.SkyboxIntensity);
			v("AmbientColor", c.AmbientColor);
			v("Exposure", c.Exposure);
			v("Fog", Group);
			v("FogDensity", c.FogDensity);
			v("FogColor", c.FogColor);
			v("FogHeight", c.FogHeight, "m");
			v("FogHeightFalloff", c.FogHeightFalloff);
			v("VolumetricFog", c.VolumetricFog);
			v("FogScattering", c.FogScattering);
			v("FogAnisotropy", c.FogAnisotropy);
			v("Post-processing", Group);
			v("SSAOEnabled", c.SSAOEnabled);
			v("SSAORadius", c.SSAORadius);
			v("SSAOIntensity", c.SSAOIntensity);
			v("BloomIntensity", c.BloomIntensity);
			v("Sound", Group);
			v("AmbientNoise", c.AmbientNoise);
		}

		template<typename V>
		void VisitFields(RigidBodyComponent& c, V&& v)
		{
			v("Type", c.Type);
			v("Mass", c.Mass, "kg");
			v("LinearDamping", c.LinearDamping);
			v("AngularDamping", c.AngularDamping);
			v("Friction", c.Friction);
			v("Restitution", c.Restitution);
			v("GravityFactor", c.GravityFactor);
			v("IsTrigger", c.IsTrigger);
			v("ContinuousCollision", c.ContinuousCollision);
			v("TransparentToSight", c.TransparentToSight);
		}

		template<typename V>
		void VisitFields(BoxColliderComponent& c, V&& v)
		{
			v("HalfExtents", c.HalfExtents, "m");
			v("Offset", c.Offset, "m");
		}

		template<typename V>
		void VisitFields(SphereColliderComponent& c, V&& v)
		{
			v("Radius", c.Radius, "m");
			v("Offset", c.Offset, "m");
		}

		template<typename V>
		void VisitFields(CapsuleColliderComponent& c, V&& v)
		{
			v("Radius", c.Radius, "m");
			v("HalfHeight", c.HalfHeight, "m");
			v("Offset", c.Offset, "m");
		}

		template<typename V> void VisitFields(MeshColliderComponent& c, V&& v) { v("UseMeshComponent", c.UseMeshComponent); }

		template<typename V>
		void VisitFields(CharacterControllerComponent& c, V&& v)
		{
			v("Radius", c.Radius, "m");
			v("Height", c.Height, "m");
			v("MaxSlopeDegrees", c.MaxSlopeDegrees, "deg");
			v("StepHeight", c.StepHeight, "m");
			v("GravityFactor", c.GravityFactor);
			v("Velocity", c.RuntimeVelocity, FieldKind::Runtime);
			v("Grounded", c.RuntimeGrounded, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(ScriptComponent& c, V&& v)
		{
			v("Script", c.Script);
			v("Properties", c.Properties);
		}

		template<typename V>
		void VisitFields(AudioSourceComponent& c, V&& v)
		{
			v("Playback", Group);
			v("Clip", c.Clip);
			v("Volume", c.Volume);
			v("Pitch", c.Pitch);
			v("Loop", c.Loop);
			v("PlayOnStart", c.PlayOnStart);
			v("Distance", Group);
			v("Spatial", c.Spatial);
			v("MinDistance", c.MinDistance, "m");
			v("MaxDistance", c.MaxDistance, "m");
			v("Guards", Group);
			v("NoiseLoudness", c.NoiseLoudness, "0-10");
		}

		template<typename V> void VisitFields(AudioListenerComponent& c, V&& v) { v("Active", c.Active); }

		template<typename V>
		void VisitFields(PerceptionComponent& c, V&& v)
		{
			v("Sight", Group);
			v("EyeHeight", c.EyeHeight, "m");
			v("ViewDistance", c.ViewDistance, "m");
			v("FieldOfView", c.FieldOfView, "deg");
			v("FocusFieldOfView", c.FocusFieldOfView, "deg");
			v("PeripheralContrast", c.PeripheralContrast);
			v("PeripheralFactor", c.PeripheralFactor);
			v("ContrastMin", c.ContrastMin);
			v("DarkSensitivity", c.DarkSensitivity);
			v("GlareSensitivity", c.GlareSensitivity);
			v("LuminanceFloor", c.LuminanceFloor);
			v("SelfShading", c.SelfShading);
			v("MotionFactor", c.MotionFactor);
			v("ReferenceAngularSize", c.ReferenceAngularSize, "rad");
			v("Adaptation to light", Group);
			v("AdaptBrightTime", c.AdaptBrightTime, "s");
			v("AdaptDarkTime", c.AdaptDarkTime, "s");
			v("Suspicion", Group);
			v("SuspicionRise", c.SuspicionRise);
			v("SuspicionFloor", c.SuspicionFloor);
			v("SuspicionDecay", c.SuspicionDecay);
			v("SuspiciousThreshold", c.SuspiciousThreshold);
			v("Hearing", Group);
			v("HearingSensitivity", c.HearingSensitivity);
			v("Reactions", Group);
			v("ReactionDuration", c.ReactionDuration, "s");
			v("InvestigateLevel", c.InvestigateLevel, "0-10");
			v("SearchLevel", c.SearchLevel, "0-10");
			v("AlarmLevel", c.AlarmLevel, "0-10");
			v("AlertMemoryTime", c.AlertMemoryTime, "s");
			v("Diligence", c.Diligence);
			v("BodyAlarm", c.BodyAlarm, "0-10");
			v("ProjectileAlarm", c.ProjectileAlarm, "0-10");
			v("Touch and close range", Group);
			v("TouchRange", c.TouchRange, "m");
			v("TouchAlarm", c.TouchAlarm, "0-10");
			v("CloseRange", c.CloseRange, "m");
			v("CloseRangeStimulus", c.CloseRangeStimulus, "0-10");
			v("Adaptation", c.RuntimeAdaptation, FieldKind::Runtime);
			v("Suspicion", c.RuntimeSuspicion, FieldKind::Runtime);
			v("Stimulus", c.RuntimeStimulus, FieldKind::Runtime);
			v("VisibleFraction", c.RuntimeVisibleFraction, FieldKind::Runtime);
			v("Contrast", c.RuntimeContrast, FieldKind::Runtime);
			v("State", c.RuntimeState, FieldKind::Runtime);
			v("ReactionStrength", c.RuntimeReactionStrength, FieldKind::Runtime);
			v("ReactionTime", c.RuntimeReactionTime, FieldKind::Runtime);
			v("StimulusKind", c.RuntimeStimulusKind, FieldKind::Runtime);
			v("StimulusCause", c.RuntimeStimulusCause, FieldKind::Runtime);
			v("Alertness", c.RuntimeAlertness, FieldKind::Runtime);
			v("LastStimulusPosition", c.RuntimeLastStimulusPosition, FieldKind::Runtime);
			v("HasStimulus", c.RuntimeHasStimulus, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(PerceptionTargetComponent& c, V&& v)
		{
			v("Kind", c.Kind);
			v("SilhouettePoints", c.SilhouettePoints);
			v("Reflectance", c.Reflectance);
			v("Radius", c.Radius, "m");
			v("Emissive", c.Emissive);
			v("LightLevel", c.RuntimeLightLevel, FieldKind::Runtime);
			v("LightGem", c.RuntimeLightGem, FieldKind::Runtime);
			v("Exposure", c.RuntimeExposure, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(SurfaceComponent& c, V&& v)
		{
			v("Material", c.Material);
			v("NoiseModifier", c.NoiseModifier, "levels");
			v("ImpactLoudness", c.ImpactLoudness, "0-10");
			v("ImpactSound", c.ImpactSound);
			v("SoundLoss", c.SoundLoss);
		}

		template<typename V>
		void VisitFields(NavMeshComponent& c, V&& v)
		{
			v("CellSize", c.CellSize, "m");
			v("CellHeight", c.CellHeight, "m");
			v("AgentRadius", c.AgentRadius, "m");
			v("AgentHeight", c.AgentHeight, "m");
			v("AgentMaxClimb", c.AgentMaxClimb, "m");
			v("AgentMaxSlope", c.AgentMaxSlope, "deg");
			v("Polygons", c.RuntimePolygons, FieldKind::Runtime);
			v("BuildMilliseconds", c.RuntimeBuildMilliseconds, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(NavAgentComponent& c, V&& v)
		{
			v("Speed", c.Speed, "m/s");
			v("TurnSpeed", c.TurnSpeed, "deg/s");
			v("StoppingDistance", c.StoppingDistance, "m");
			v("FaceMovement", c.FaceMovement);
			v("Status", c.RuntimeStatus, FieldKind::Runtime);
			v("Destination", c.RuntimeDestination, FieldKind::Runtime);
			v("RemainingDistance", c.RuntimeRemainingDistance, FieldKind::Runtime);
			v("PartialPath", c.RuntimePartialPath, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(VitalityComponent& c, V&& v)
		{
			v("State", c.State);
			v("Health", c.Health);
		}

		template<typename V>
		void VisitFields(FogOfWarComponent& c, V&& v)
		{
			v("View", Group);
			v("Enabled", c.Enabled);
			v("EyeHeight", c.EyeHeight, "m");
			v("Lean", c.Lean);
			v("ViewAngle", c.ViewAngle, "deg");
			v("EdgeSoftness", c.EdgeSoftness, "deg");
			v("ViewDistance", c.ViewDistance, "m");
			v("AwarenessRadius", c.AwarenessRadius, "m");
			v("ConeFromCamera", c.ConeFromCamera);
			v("SeeOverHeight", c.SeeOverHeight, "m");
			v("EyeSpeed", c.EyeSpeed, "/s");
			v("What the thief does not see", Group);
			v("Brightness", c.Brightness);
			v("Saturation", c.Saturation);
			v("HideActors", c.HideActors);
			v("MemoryTime", c.MemoryTime, "s");
			v("Active", c.RuntimeActive, FieldKind::Runtime);
			v("Eye", c.RuntimeEye, FieldKind::Runtime);
			v("Forward", c.RuntimeForward, FieldKind::Runtime);
			v("EyeHeightNow", c.RuntimeEyeHeight, FieldKind::Runtime);
			v("HiddenActors", c.RuntimeHiddenActors, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(RagdollComponent& c, V&& v)
		{
			v("Enabled", c.Enabled);
			v("Mass", c.Mass, "kg");
			v("RadiusScale", c.RadiusScale);
			v("Friction", c.Friction);
			v("Active", c.RuntimeActive, FieldKind::Runtime);
			v("Awake", c.RuntimeAwake, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(FogVolumeComponent& c, V&& v)
		{
			v("Mode", c.Mode);
			v("HalfExtents", c.HalfExtents, "m");
			v("Density", c.Density);
			v("EdgeFade", c.EdgeFade, "m");
			v("Enabled", c.Enabled);
		}

		template<typename V>
		void VisitFields(SmokeVolumeComponent& c, V&& v)
		{
			v("Radius", c.Radius, "m");
			v("Density", c.Density);
			v("Lifetime", c.Lifetime, "s");
			v("Age", c.RuntimeAge, FieldKind::Runtime);
		}

		template<typename V>
		void VisitFields(FlameComponent& c, V&& v)
		{
			v("Height", c.Height, "m");
			v("Radius", c.Radius, "m");
			v("Intensity", c.Intensity);
			v("Speed", c.Speed);
			v("Color", c.Color);
			v("Enabled", c.Enabled);
		}

		template<typename V>
		void VisitFields(StimSourceComponent& c, V&& v)
		{
			v("Stim", c.Stim);
			v("Intensity", c.Intensity);
			v("Delivery", c.Delivery);
			v("Radius", c.Radius, "m");
			v("Enabled", c.Enabled);
			v("DestroyOnContact", c.DestroyOnContact);
			v("RequireLineOfSight", c.RequireLineOfSight);
		}

		template<typename V> void VisitFields(StimResponseComponent& c, V&& v) { v("Rules", c.Rules); }
		template<typename V> void VisitFields(PrefabComponent& c, V&& v) { v("Prefab", c.Prefab); }

	}

	// Nested structs used inside vectors.
	template<typename V>
	void VisitFields(StimAction& c, V&& v)
	{
		v("Type", c.Type);
		v("Asset", c.Asset);
		v("Value", c.Value);
		v("Radius", c.Radius);
	}

	template<typename V>
	void VisitFields(StimResponseRule& c, V&& v)
	{
		v("Stim", c.Stim);
		v("Threshold", c.Threshold);
		v("Once", c.Once);
		v("Actions", c.Actions);
	}

	void ToJson(json& out, const StimAction& value)
	{
		out = json::object();
		VisitFields(const_cast<StimAction&>(value), [&](const char* name, auto& field) { ToJson(out[name], field); });
	}

	bool FromJson(const json& in, StimAction& value, std::string& error)
	{
		return JsonDetail::ReadObject(in, value, error, [](StimAction& target, auto&& visitor) { VisitFields(target, visitor); });
	}

	void ToJson(json& out, const StimResponseRule& value)
	{
		out = json::object();
		VisitFields(const_cast<StimResponseRule&>(value), [&](const char* name, auto& field) { ToJson(out[name], field); });
	}

	bool FromJson(const json& in, StimResponseRule& value, std::string& error)
	{
		return JsonDetail::ReadObject(in, value, error, [](StimResponseRule& target, auto&& visitor) { VisitFields(target, visitor); });
	}

	namespace {

		template<typename T>
		json SerializeComponent(const T& component, const ComponentSerializeOptions& options)
		{
			json out = json::object();
			VisitFields(const_cast<T&>(component), [&](const char* name, auto& field, FieldMeta meta = {}) {
				if constexpr (!IsGroup<decltype(field)>)
				{
					if (meta.Kind == FieldKind::Runtime && !options.IncludeRuntimeState)
						return;
					ToJson(out[name], field);
				}
			});
			return out;
		}

		template<typename T>
		bool DeserializeComponent(T& component, const json& in, std::string& error)
		{
			if (!in.is_object())
			{
				error = "component data must be a JSON object";
				return false;
			}

			// Work on a copy so a failed merge leaves the component untouched.
			T updated = component;
			std::vector<std::string> known;
			bool ok = true;
			VisitFields(updated, [&](const char* name, auto& field, FieldMeta meta = {}) {
				if constexpr (!IsGroup<decltype(field)>)
				{
					known.emplace_back(name);
					if (!ok || meta.Kind == FieldKind::Runtime)
						return;
					const auto it = in.find(name);
					if (it == in.end())
						return;
					std::string fieldError;
					if (!FromJson(*it, field, fieldError))
					{
						error = std::format("field '{}': {}", name, fieldError);
						ok = false;
					}
				}
			});
			if (!ok)
				return false;

			for (const auto& [key, value] : in.items())
			{
				if (std::find(known.begin(), known.end(), key) == known.end())
				{
					error = std::format("unknown field '{}'", key);
					return false;
				}
			}
			component = std::move(updated);
			return true;
		}

		template<typename T>
		ComponentInfo MakeInfo(std::string name, bool removable = true)
		{
			ComponentInfo info;
			info.Name = std::move(name);
			info.Removable = removable;
			info.Has = [](Entity entity) { return entity.HasComponent<T>(); };
			info.Add = [](Entity entity) {
				if (!entity.HasComponent<T>())
					entity.AddComponent<T>();
			};
			info.Remove = [](Entity entity) { entity.RemoveComponent<T>(); };
			info.Serialize = [](Entity entity, const ComponentSerializeOptions& options) {
				return SerializeComponent(entity.GetComponent<T>(), options);
			};
			info.Deserialize = [](Entity entity, const json& in, std::string& error) {
				return DeserializeComponent(entity.GetComponent<T>(), in, error);
			};
			info.Defaults = []() { return SerializeComponent(T{}, ComponentSerializeOptions{}); };
			T component{};
			std::string group;
			VisitFields(component, [&](const char* fieldName, auto& field, FieldMeta meta = {}) {
				if constexpr (IsGroup<decltype(field)>)
					group = fieldName;
				else
					info.Fields.push_back({ fieldName, group, meta.Unit ? meta.Unit : "", meta.Kind == FieldKind::Runtime });
			});
			return info;
		}

		std::vector<ComponentInfo> BuildRegistry()
		{
			std::vector<ComponentInfo> registry;
			registry.push_back(MakeInfo<TagComponent>("Tag", false));
			registry.push_back(MakeInfo<TransformComponent>("Transform", false));
			registry.push_back(MakeInfo<CameraComponent>("Camera"));
			registry.push_back(MakeInfo<MeshComponent>("Mesh"));
			registry.push_back(MakeInfo<AnimatorComponent>("Animator"));
			registry.push_back(MakeInfo<MaterialComponent>("Material"));
			registry.push_back(MakeInfo<DirectionalLightComponent>("DirectionalLight"));
			registry.push_back(MakeInfo<PointLightComponent>("PointLight"));
			registry.push_back(MakeInfo<SpotLightComponent>("SpotLight"));
			registry.push_back(MakeInfo<EnvironmentComponent>("Environment"));
			registry.push_back(MakeInfo<RigidBodyComponent>("RigidBody"));
			registry.push_back(MakeInfo<BoxColliderComponent>("BoxCollider"));
			registry.push_back(MakeInfo<SphereColliderComponent>("SphereCollider"));
			registry.push_back(MakeInfo<CapsuleColliderComponent>("CapsuleCollider"));
			registry.push_back(MakeInfo<MeshColliderComponent>("MeshCollider"));
			registry.push_back(MakeInfo<CharacterControllerComponent>("CharacterController"));
			registry.push_back(MakeInfo<ScriptComponent>("Script"));
			registry.push_back(MakeInfo<AudioSourceComponent>("AudioSource"));
			registry.push_back(MakeInfo<AudioListenerComponent>("AudioListener"));
			registry.push_back(MakeInfo<PerceptionComponent>("Perception"));
			registry.push_back(MakeInfo<PerceptionTargetComponent>("PerceptionTarget"));
			registry.push_back(MakeInfo<VitalityComponent>("Vitality"));
			registry.push_back(MakeInfo<RagdollComponent>("Ragdoll"));
			registry.push_back(MakeInfo<FogOfWarComponent>("FogOfWar"));
			registry.push_back(MakeInfo<SmokeVolumeComponent>("SmokeVolume"));
			registry.push_back(MakeInfo<FogVolumeComponent>("FogVolume"));
			registry.push_back(MakeInfo<FlameComponent>("Flame"));
			registry.push_back(MakeInfo<StimSourceComponent>("StimSource"));
			registry.push_back(MakeInfo<StimResponseComponent>("StimResponse"));
			registry.push_back(MakeInfo<SurfaceComponent>("Surface"));
			registry.push_back(MakeInfo<NavMeshComponent>("NavMesh"));
			registry.push_back(MakeInfo<NavAgentComponent>("NavAgent"));
			registry.push_back(MakeInfo<PrefabComponent>("Prefab"));
			return registry;
		}

	}

	const std::vector<ComponentInfo>& ComponentRegistry::GetAll()
	{
		static const std::vector<ComponentInfo> s_Registry = BuildRegistry();
		return s_Registry;
	}

	const ComponentInfo* ComponentRegistry::Find(std::string_view name)
	{
		for (const ComponentInfo& info : GetAll())
		{
			if (info.Name == name)
				return &info;
		}
		return nullptr;
	}

	json ComponentRegistry::SerializeEntityComponents(Entity entity, const ComponentSerializeOptions& options)
	{
		json components = json::object();
		for (const ComponentInfo& info : GetAll())
		{
			if (info.Has(entity))
				components[info.Name] = info.Serialize(entity, options);
		}
		return components;
	}

	bool ComponentRegistry::DeserializeEntityComponents(Entity entity, const json& components, std::string& error)
	{
		if (!components.is_object())
		{
			error = "'Components' must be a JSON object";
			return false;
		}
		for (const auto& [name, data] : components.items())
		{
			const ComponentInfo* info = Find(name);
			if (!info)
			{
				error = std::format("unknown component '{}'", name);
				return false;
			}
			info->Add(entity);
			std::string componentError;
			if (!info->Deserialize(entity, data, componentError))
			{
				error = std::format("{}: {}", name, componentError);
				return false;
			}
		}
		return true;
	}

}
