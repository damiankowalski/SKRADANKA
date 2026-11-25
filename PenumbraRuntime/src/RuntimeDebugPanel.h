#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/KeyCodes.h"
#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Core/UserSettings.h"
#include "Penumbra/Debug/DebugOverlays.h"
#include "Penumbra/Debug/FreeCamera.h"
#include "Penumbra/Debug/NoiseProbe.h"
#include "Penumbra/Debug/PerformanceMonitor.h"
#include "Penumbra/Renderer/RenderCamera.h"
#include "Penumbra/Renderer/RenderDevice.h"

#include <imgui.h>

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace Penumbra {

	class ImGuiHost;
	class Scene;
	class SceneRenderer;

	// The runtime's testing tools, drawn with Dear ImGui over the game:
	//   F3  performance overlay: FPS, frame times, CPU per system, process CPU and memory, GPU time and memory
	//   F4  debug panel: what guards see and hear, test noises clicked into the world, simulation speed,
	//       and the inspector: click an object to edit its components live, right-click for actions
	//   F5  freelook: the camera leaves the thief and flies through the level (RMB look, WASD, Q/E, Shift)
	//   F6  pause / resume the simulation
	//   F7  fog of war on / off (what the thief sees, or the whole level)
	// While the panel or freelook is up the game gets no keyboard or mouse input (Input::SetGameInputBlocked).
	class RuntimeDebugPanel
	{
	public:
		// `settings` (UI size, 3D resolution) is the runtime's; the panel edits and saves it.
		RuntimeDebugPanel(GLFWwindow* window, nvrhi::IDevice* device, UserSettings& settings);
		~RuntimeDebugPanel();

		RuntimeDebugPanel(const RuntimeDebugPanel&) = delete;
		RuntimeDebugPanel& operator=(const RuntimeDebugPanel&) = delete;

		// Opens tools by name, comma separated: "performance", "panel", "freelook" (unknown names are logged).
		void Open(std::string_view tools);
		// A function key the panel owns; true when it was handled.
		bool OnKeyPressed(KeyCode key, Scene* scene);
		// A new scene started: settings that live in the scene (perception's notarget) are applied again.
		void OnSceneLoaded(Scene& scene);

		// The simulation step after the panel's time scale (pause is the scene's own).
		Timestep ScaleTimestep(Timestep ts) const { return Timestep(ts * m_TimeScale); }
		bool BlocksGameInput() const { return m_PanelOpen || m_FreeLook; }
		// The cursor state the panel needs, or empty to leave it to the game's scripts.
		std::optional<bool> GetCursorOverride() const;

		// After the simulation step: records the frame and the noises made in it.
		void AfterUpdate(Scene& scene, float frameMs, float updateMs);
		// Before rendering: starts the UI frame, moves the free camera, draws world overlays. Returns
		// the camera to render with (the free camera, or the scene's).
		RenderCamera BeginFrame(Scene& scene, SceneRenderer& renderer, float aspect);
		// After the scene is presented: draws the panels on top of the back buffer.
		void EndFrame(Scene& scene, SceneRenderer& renderer, RenderDevice& device, const RenderCamera& camera,
			nvrhi::IFramebuffer* target, float renderMs);

	private:
		void UpdateStyle();
		float UiScale() const;
		void DrawPerformance(Scene& scene, SceneRenderer& renderer, RenderDevice& device);
		void DrawPanel(Scene& scene);
		void DrawFogOfWar(Scene& scene);
		void DrawFog(Scene& scene);
		void DrawWeather(Scene& scene);
		void DrawGuardLabels(Scene& scene, const RenderCamera& camera);
		void HandleWorldClick(Scene& scene, const RenderCamera& camera);
		std::optional<glm::vec3> WorldPointUnderMouse(Scene& scene, const RenderCamera& camera);
		void MakeTestNoise(Scene& scene, const glm::vec3& position);
		Entity GetSelection(Scene& scene);
		void DrawInspector(Scene& scene);
		void DrawObjectMenu(Scene& scene);
		// The actions on an object, as menu items (the right-click menu) or buttons (the inspector).
		void DrawObjectActions(Scene& scene, Entity entity, bool asMenu);
		void SetPaused(Scene& scene, bool paused);

	private:
		Scope<ImGuiHost> m_ImGui;
		GLFWwindow* m_Window = nullptr;
		UserSettings& m_Settings;

		// Performance
		bool m_ShowPerformance = false;
		PerformanceMonitor m_Monitor;
		PerformanceSample m_PendingSample;
		VideoMemoryInfo m_VideoMemory;
		bool m_HasVideoMemory = false;
		double m_VideoMemoryTime = -1.0;
		uint64_t m_PhysicalMemory = 0;

		// Panel
		bool m_PanelOpen = false;
		bool m_ShowVision = true;
		bool m_ShowLabels = true;
		bool m_ShowNoises = true;
		bool m_ShowAllNoises = true; // every noise the game makes, not only the test ones
		bool m_ShowNavMesh = false;
		bool m_ShowColliders = false;
		bool m_IgnoreThief = false;
		bool m_FogGlow = true; // guards see the fog's glow (PerceptionSystem::SetFogGlow)
		bool m_FogOfWar = true; // off: the whole level is drawn, whatever the thief's FogOfWar component says
		float m_TimeScale = 1.0f;

		// Inspector
		enum class ClickMode
		{
			Select,
			Noise,
		};
		ClickMode m_ClickMode = ClickMode::Select; // what a left click in the world does (Shift+click: always a noise)
		UUID m_Selection = UUID::Invalid;
		UUID m_InspectedSelection = UUID::Invalid; // the selection the Inspector tab last showed
		bool m_InspectedPlacing = false;
		bool m_Placing = false;             // "Move here": the next left click puts the selection there
		bool m_OnlySelectedVision = false;  // draw the selected NPC's view fan alone
		bool m_OpenObjectMenu = false;

		// Test noises
		float m_NoiseLoudness = 5.0f;
		std::vector<NoiseHearing> m_LastHearing;
		std::optional<glm::vec3> m_LastNoisePosition;
		float m_LastNoiseLoudness = 0.0f;
		std::vector<DebugNoise> m_Noises; // drawn as rings for NoiseLifetime seconds
		struct LoggedNoise
		{
			float Time = 0.0f;
			float Loudness = 0.0f;
			std::string Cause;
		};
		std::deque<LoggedNoise> m_NoiseLog;
		uint64_t m_LastNoiseFrame = ~0ull;
		static constexpr float NoiseLifetime = 3.0f;

		// Freelook
		float m_FlashHold = 0.0f; // the weather section's flash length (0: the level's own)
		bool m_FreeLook = false;
		bool m_FreeLookStarted = false;
		bool m_Looking = false; // right mouse button held: the cursor is captured for looking
		FreeCamera m_FreeCamera;
		RenderCamera m_LastSceneCamera;
	};

}
