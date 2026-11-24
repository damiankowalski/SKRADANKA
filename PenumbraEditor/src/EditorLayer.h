#pragma once

#include "EditorCamera.h"
#include "Panels/PropertiesPanel.h"

#include "Penumbra/Automation/AutomationHost.h"
#include "Penumbra/Automation/CommandProcessor.h"
#include "Penumbra/Automation/CommandServer.h"
#include "Penumbra/Core/Layer.h"
#include "Penumbra/Core/UserSettings.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/Scene.h"

#include <imgui.h>

#include <deque>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace Penumbra {

	class ImGuiHost;

	struct EditorOptions
	{
		std::filesystem::path ProjectPath;
		uint16_t CommandPort = 7777;
		bool EnableCommandServer = true;
	};

	class EditorLayer final : public Layer, public AutomationHost
	{
	public:
		explicit EditorLayer(EditorOptions options);
		~EditorLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnRender() override;
		void OnEvent(Event& event) override;

		// AutomationHost
		Ref<Scene> GetEditScene() override { return m_EditScene; }
		void SetEditScene(Ref<Scene> scene) override;
		Ref<Scene> GetPlayScene() override { return m_PlayScene; }
		bool StartPlay() override;
		void StopPlay() override;
		RenderDevice* GetRenderDevice() override;
		SceneRenderer* GetRenderer() override { return m_Renderer.get(); }
		void SetSelection(UUID entity) override { m_Selection = entity; }

	private:
		Ref<Scene> GetActiveSceneForView() { return m_PlayScene ? m_PlayScene : m_EditScene; }
		Entity GetSelectedEntity();

		// UI
		void DrawMenuBar();
		// The default docked layout on the first start (no saved one) and after View > Reset layout.
		void DockPanels();
		// What Create offers (the menu bar and the right-click menu on empty Hierarchy space).
		void DrawCreateMenuItems();
		void InstantiatePrefab(const std::string& path);
		void CreateScriptFile(const std::filesystem::path& folder);
		// Play controls, gizmo modes and the frame time along the top of the viewport; returns its height.
		float DrawViewportToolbar(const ImVec2& origin, const ImVec2& size);
		void DrawViewport();
		// The 3D picture's size in pixels: the viewport panel times the framebuffer and render scales.
		glm::uvec2 GetViewportPixels() const;
		void DrawHierarchy();
		void DrawHierarchyNode(Entity entity);
		void DrawContentBrowser();
		void DrawConsole();
		void DrawStatistics();
		void DrawPathPopup();
		void DrawDebugOverlays(Scene& scene);

		// Actions
		Entity CreatePrimitive(const std::string& name, const std::string& mesh);
		Entity CreateLight(const std::string& type);
		void PickEntity(const glm::vec2& viewportPosition);
		void SaveScene();
		void SaveSceneAs(const std::string& path);
		void OpenScene(const std::string& path);
		void OpenProject(const std::filesystem::path& path);
		void ExportGame(const std::string& output);

		// Undo
		// Called before any edit: the first call of an interaction stores the pre-edit snapshot.
		void MarkChanged();
		void CommitPendingChange();
		void ResetUndoHistory();
		void Undo();
		void Redo();
		void RestoreSnapshot(const nlohmann::json& snapshot);
		// Runs an automation command; commands that modify the edit scene become one undo step.
		nlohmann::json ExecuteRecorded(const nlohmann::json& request);

	private:
		EditorOptions m_Options;
		Scope<ImGuiHost> m_ImGui;
		UserSettings m_Settings;
		Scope<SceneRenderer> m_Renderer;
		Scope<CommandProcessor> m_Commands;
		CommandServer m_Server;
		EditorCamera m_Camera;
		PropertiesPanel m_Properties;

		Ref<Scene> m_EditScene;
		Ref<Scene> m_PlayScene;
		UUID m_Selection = UUID::Invalid;

		glm::vec2 m_ViewportSize{ 1280.0f, 720.0f }; // window units (ImGui)
		glm::vec2 m_ViewportOrigin{ 0.0f };
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;
		int m_GizmoOperation = 0; // ImGuizmo::OPERATION
		bool m_GizmoLocal = false;
		bool m_ShowGrid = true;
		bool m_LayoutChecked = false;
		bool m_ResetLayout = false;
		bool m_ShowColliders = false;
		bool m_ShowPerception = true;
		bool m_ShowNavMesh = false;
		bool m_UseSceneCamera = false;

		std::deque<nlohmann::json> m_UndoStack;
		std::deque<nlohmann::json> m_RedoStack;
		nlohmann::json m_StableSnapshot; // edit scene state before the current change
		bool m_ChangeOpen = false;
		float m_HotReloadTimer = 0.0f;

		struct ConsoleLine
		{
			LogLevel Level;
			std::string Text;
		};
		std::mutex m_ConsoleMutex;
		std::deque<ConsoleLine> m_Console;
		Log::SinkHandle m_ConsoleSink = 0;
		char m_ConsoleInput[1024] = {};
		char m_ConsoleFilter[128] = {};
		bool m_ConsoleShowInfo = true;
		bool m_ConsoleShowWarnings = true;
		bool m_ConsoleShowErrors = true;

		char m_HierarchyFilter[128] = {};
		UUID m_Renaming = UUID::Invalid; // the entity whose name is being edited in Hierarchy
		char m_RenameBuffer[256] = {};
		bool m_RenameFocus = false;

		std::string m_PathPopupTitle;
		char m_PathBuffer[512] = {};
		std::function<void(const std::string&)> m_PathPopupAction;
		bool m_OpenPathPopup = false;

		float m_FrameTime = 0.0f;
	};

}
