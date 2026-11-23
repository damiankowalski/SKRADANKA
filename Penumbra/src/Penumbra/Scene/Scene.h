#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Scene/Components.h"
#include "Penumbra/Scene/GameState.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Penumbra {

	class Entity;
	class PhysicsWorld;
	class ScriptEngine;
	class AudioScene;
	class PerceptionSystem;
	class FogOfWarSystem;
	class StimSystem;
	class NavigationSystem;
	class OverlayList;

	// Milliseconds each system took in the last runtime frame (Scene::RunRuntimeFrame), for the
	// runtime's performance monitor. Contacts covers collision callbacks, contact stims and impact noise.
	struct SceneFrameProfile
	{
		float Scripts = 0.0f;
		float Navigation = 0.0f;
		float Physics = 0.0f;
		float Contacts = 0.0f;
		float Stims = 0.0f;
		float Perception = 0.0f;
		float Smoke = 0.0f;
		float Audio = 0.0f;
		float Ragdolls = 0.0f;
		float Animation = 0.0f;
		float FogOfWar = 0.0f;
		float Destruction = 0.0f;

		float Total() const
		{
			return Scripts + Navigation + Physics + Contacts + Stims + Perception + Smoke + Audio + Ragdolls + Animation + FogOfWar + Destruction;
		}
	};

	struct TestReport
	{
		bool Completed = false;
		bool Passed = false;
		uint32_t Assertions = 0;
		uint32_t Failures = 0;
		std::vector<std::string> Messages;
	};

	class Scene
	{
	public:
		Scene();
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Deep copy used when entering play mode: same UUIDs, no runtime state.
		static Ref<Scene> Copy(const Ref<Scene>& other);

		Entity CreateEntity(const std::string& name = "Entity");
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
		// Destroys the entity and all of its children. During runtime updates destruction
		// is deferred to the end of the frame so systems never see dangling entities.
		void DestroyEntity(Entity entity);
		Entity DuplicateEntity(Entity entity);

		Entity GetEntityByUUID(UUID uuid);
		std::optional<Entity> TryGetEntityByUUID(UUID uuid);
		Entity FindEntityByName(std::string_view name);
		std::vector<Entity> GetAllEntities();
		std::vector<Entity> GetRootEntities();
		size_t GetEntityCount() const { return m_EntityMap.size(); }
		bool IsEntityPendingDestroy(Entity entity) const;

		// Hierarchy
		void SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
		void Unparent(Entity child, bool keepWorldTransform = true);
		bool IsAncestor(Entity ancestor, Entity entity);
		glm::mat4 GetWorldTransform(Entity entity);
		glm::vec3 GetWorldPosition(Entity entity);
		glm::quat GetWorldRotation(Entity entity);
		void SetWorldTransform(Entity entity, const glm::mat4& worldTransform);

		Entity GetPrimaryCameraEntity();
		Entity GetEnvironmentEntity();

		// Runtime
		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdateRuntime(Timestep ts);
		void OnUpdateEditor(Timestep ts);
		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }
		void SetPaused(bool paused) { m_IsPaused = paused; }
		// Advances a paused runtime scene by the given number of frames.
		void Step(int frames = 1) { m_StepFrames = frames; }

		float GetTime() const { return m_Time; }

		// Randomness. Lua's math.random and engine-side rolls (NextRandom) start from this seed when
		// the game starts. It is fixed by default so tests and headless runs are reproducible; the
		// interactive runtime and the editor's play mode pick a new one each time (see --seed).
		static constexpr uint64_t DefaultRandomSeed = 0x5EED;
		void SetRandomSeed(uint64_t seed) { m_RandomSeed = seed; }
		uint64_t GetRandomSeed() const { return m_RandomSeed; }
		// Deterministic value in [0, 1) from the scene's generator.
		float NextRandom();
		uint64_t GetFrameCount() const { return m_FrameCount; }
		const SceneFrameProfile& GetFrameProfile() const { return m_FrameProfile; }

		void OnViewportResize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }
		// Logical-to-physical pixel factor of the HUD (OverlayScaleFor): 1 up to 1080 lines.
		float GetUIScale() const;

		// Requests (made by scripts) that the host application acts on between frames.
		void RequestSceneLoad(const std::string& scenePath) { m_RequestedScene = scenePath; }
		const std::optional<std::string>& GetRequestedScene() const { return m_RequestedScene; }
		void ClearRequestedScene() { m_RequestedScene.reset(); }
		void RequestQuit(int exitCode) { m_RequestedQuit = exitCode; }
		const std::optional<int>& GetRequestedQuit() const { return m_RequestedQuit; }
		void RequestCursorLock(bool locked) { m_RequestedCursorLock = locked; }
		std::optional<bool> ConsumeCursorLockRequest();

		// Values kept across scene loads (GameState.h). The host passes them on to the next scene.
		GameState& GetGameState() { return *m_GameState; }
		const Ref<GameState>& GetGameStateRef() const { return m_GameState; }
		void SetGameState(Ref<GameState> state)
		{
			if (state)
				m_GameState = std::move(state);
		}

		TestReport& GetTestReport() { return m_TestReport; }

		// Lines scripts record for the host (Lua playtest.record: the agent playtester's JSONL, docs/Playtest.md).
		// Without a sink nothing is recorded: game scripts still have no way to write files.
		using RecordSink = std::function<void(const std::string&)>;
		void SetRecordSink(RecordSink sink) { m_RecordSink = std::move(sink); }
		bool HasRecordSink() const { return static_cast<bool>(m_RecordSink); }
		void Record(const std::string& line)
		{
			if (m_RecordSink)
				m_RecordSink(line);
		}

		PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
		ScriptEngine* GetScriptEngine() const { return m_ScriptEngine.get(); }
		AudioScene* GetAudioScene() const { return m_AudioScene.get(); }
		PerceptionSystem* GetPerceptionSystem() const { return m_PerceptionSystem.get(); }
		FogOfWarSystem* GetFogOfWarSystem() const { return m_FogOfWarSystem.get(); }
		StimSystem* GetStimSystem() const { return m_StimSystem.get(); }
		NavigationSystem* GetNavigationSystem() const { return m_NavigationSystem.get(); }
		OverlayList& GetOverlay() { return *m_Overlay; }

		// Headless runs (tests, CI) create the audio engine without an output device.
		void SetAudioDeviceEnabled(bool enabled) { m_AudioDeviceEnabled = enabled; }

		entt::registry& GetRegistry() { return m_Registry; }

		const std::filesystem::path& GetFilePath() const { return m_FilePath; }
		void SetFilePath(const std::filesystem::path& path) { m_FilePath = path; }

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

	private:
		void DestroyEntityImmediate(Entity entity);
		void FlushPendingDestroys();
		void UpdateSmokeVolumes(Timestep ts);
		// Dynamic objects with a Surface ImpactLoudness make a noise (and sound) when they hit something.
		void EmitImpactNoise(UUID object, UUID other, const glm::vec3& point, float speed);
		void RunRuntimeFrame(Timestep ts);

	private:
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<UUID> m_PendingDestroy;

		Scope<PhysicsWorld> m_PhysicsWorld;
		Scope<ScriptEngine> m_ScriptEngine;
		Scope<AudioScene> m_AudioScene;
		Scope<PerceptionSystem> m_PerceptionSystem;
		Scope<FogOfWarSystem> m_FogOfWarSystem;
		Scope<StimSystem> m_StimSystem;
		Scope<NavigationSystem> m_NavigationSystem;
		Scope<OverlayList> m_Overlay;

		bool m_IsRunning = false;
		bool m_IsPaused = false;
		bool m_IsUpdating = false;
		bool m_AudioDeviceEnabled = true;
		int m_StepFrames = 0;
		float m_Time = 0.0f;
		uint64_t m_FrameCount = 0;
		uint64_t m_RandomSeed = DefaultRandomSeed;
		uint64_t m_RandomState = DefaultRandomSeed;
		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;

		std::optional<std::string> m_RequestedScene;
		std::optional<int> m_RequestedQuit;
		std::optional<bool> m_RequestedCursorLock;
		Ref<GameState> m_GameState = CreateRef<GameState>();
		TestReport m_TestReport;
		RecordSink m_RecordSink;
		SceneFrameProfile m_FrameProfile;
		std::filesystem::path m_FilePath;

		friend class Entity;
		friend class SceneSerializer;
	};

}
