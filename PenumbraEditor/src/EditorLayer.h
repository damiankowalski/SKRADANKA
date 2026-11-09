#pragma once

#include "CommandServer.h"
#include "EditorCamera.h"
#include "Panels/PropertiesPanel.h"

#include "Penumbra/Automation/AutomationHost.h"
#include "Penumbra/Automation/CommandProcessor.h"
#include "Penumbra/Core/Layer.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/Scene.h"

#include <deque>
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
		void DrawToolbar();
		void DrawViewport();
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

	private:
		EditorOptions m_Options;
		Scope<ImGuiHost> m_ImGui;
		Scope<SceneRenderer> m_Renderer;
		Scope<CommandProcessor> m_Commands;
		CommandServer m_Server;
		EditorCamera m_Camera;
		PropertiesPanel m_Properties;

		Ref<Scene> m_EditScene;
		Ref<Scene> m_PlayScene;
		UUID m_Selection = UUID::Invalid;

		glm::vec2 m_ViewportSize{ 1280.0f, 720.0f };
		glm::vec2 m_ViewportOrigin{ 0.0f };
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;
		int m_GizmoOperation = 0; // ImGuizmo::OPERATION
		bool m_GizmoLocal = false;
		bool m_GizmoWasUsing = false;
		bool m_ShowGrid = true;
		bool m_ShowColliders = false;
		bool m_ShowPerception = true;
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

		std::string m_PathPopupTitle;
		char m_PathBuffer[512] = {};
		std::function<void(const std::string&)> m_PathPopupAction;
		bool m_OpenPathPopup = false;

		float m_FrameTime = 0.0f;
	};

}
