#include "EditorLayer.h"

#include "ImGui/ImGuiHost.h"

#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Core/Application.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Project/ProjectExporter.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Scene/JsonSerialization.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>

#include <chrono>

#include <Windows.h>
#include <shellapi.h>

namespace Penumbra {

	using json = nlohmann::json;

	namespace {
		constexpr size_t MaxUndoSteps = 64;
		constexpr size_t MaxConsoleLines = 2000;

		glm::vec4 StateColor(AwarenessState state)
		{
			switch (state)
			{
				case AwarenessState::Unaware:    return { 0.3f, 0.9f, 0.4f, 1.0f };
				case AwarenessState::Suspicious: return { 1.0f, 0.9f, 0.2f, 1.0f };
				case AwarenessState::Investigating: return { 1.0f, 0.72f, 0.15f, 1.0f };
				case AwarenessState::Searching:  return { 1.0f, 0.55f, 0.1f, 1.0f };
				case AwarenessState::Alarmed:    return { 1.0f, 0.15f, 0.1f, 1.0f };
			}
			return glm::vec4(1.0f);
		}

		// Ray vs. oriented box (entity-local AABB under a world transform). Returns hit distance.
		std::optional<float> RayHitsBox(const glm::vec3& origin, const glm::vec3& direction, const BoundingBox& box, const glm::mat4& world)
		{
			const glm::mat4 inverse = glm::inverse(world);
			const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
			const glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
			float tMin = 0.0f, tMax = 1e30f;
			for (int axis = 0; axis < 3; axis++)
			{
				if (std::abs(localDirection[axis]) < 1e-8f)
				{
					if (localOrigin[axis] < box.Min[axis] || localOrigin[axis] > box.Max[axis])
						return std::nullopt;
					continue;
				}
				float t1 = (box.Min[axis] - localOrigin[axis]) / localDirection[axis];
				float t2 = (box.Max[axis] - localOrigin[axis]) / localDirection[axis];
				if (t1 > t2)
					std::swap(t1, t2);
				tMin = std::max(tMin, t1);
				tMax = std::min(tMax, t2);
				if (tMin > tMax)
					return std::nullopt;
			}
			const glm::vec3 worldHit = glm::vec3(world * glm::vec4(localOrigin + localDirection * tMin, 1.0f));
			return glm::distance(origin, worldHit);
		}
	}

	EditorLayer::EditorLayer(EditorOptions options)
		: Layer("Editor"), m_Options(std::move(options)), m_EditScene(CreateRef<Scene>())
	{
	}

	EditorLayer::~EditorLayer() = default;

	// ------------------------------------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------------------------------------

	void EditorLayer::OnAttach()
	{
		m_ConsoleSink = Log::AddSink([this](const LogMessage& message) {
			std::scoped_lock lock(m_ConsoleMutex);
			m_Console.push_back({ message.Level, std::format("[{}] {}", message.Logger, message.Text) });
			while (m_Console.size() > MaxConsoleLines)
				m_Console.pop_front();
		});

		Application& app = Application::Get();
		m_ImGui = CreateScope<ImGuiHost>(app.GetWindow()->GetNativeWindow(), app.GetRenderDevice().GetDevice());
		m_Renderer = CreateScope<SceneRenderer>(app.GetRenderDevice());
		m_Commands = CreateScope<CommandProcessor>(*this);

		if (!m_Options.ProjectPath.empty())
			OpenProject(m_Options.ProjectPath);
		ResetUndoHistory();

		if (m_Options.EnableCommandServer)
			m_Server.Start(m_Options.CommandPort);
	}

	void EditorLayer::OnDetach()
	{
		m_Server.Stop();
		StopPlay();
		Log::RemoveSink(m_ConsoleSink);
		m_Commands.reset();
		m_Renderer.reset();
		m_ImGui.reset();
	}

	RenderDevice* EditorLayer::GetRenderDevice() { return &Application::Get().GetRenderDevice(); }

	void EditorLayer::SetEditScene(Ref<Scene> scene)
	{
		StopPlay();
		m_EditScene = std::move(scene);
		m_Selection = UUID::Invalid;
		ResetUndoHistory();
		if (Window* window = Application::Get().GetWindow())
		{
			const std::string file = m_EditScene->GetFilePath().empty() ? std::string("Untitled") : m_EditScene->GetFilePath().filename().string();
			window->SetTitle(std::format("Penumbra Editor - {} - {}", Project::IsLoaded() ? Project::GetConfig().Name : std::string("No project"), file));
		}
	}

	bool EditorLayer::StartPlay()
	{
		if (m_PlayScene)
			return false;
		CommitPendingChange();
		m_PlayScene = Scene::Copy(m_EditScene);
		m_PlayScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		// Every play session plays out differently (events, search spots); headless automation keeps the fixed seed.
		m_PlayScene->SetRandomSeed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		m_PlayScene->OnRuntimeStart();
		m_UseSceneCamera = static_cast<bool>(m_PlayScene->GetPrimaryCameraEntity());
		PN_CORE_INFO("Play mode started");
		return true;
	}

	void EditorLayer::StopPlay()
	{
		if (!m_PlayScene)
			return;
		m_PlayScene->OnRuntimeStop();
		m_PlayScene.reset();
		m_UseSceneCamera = false;
		Input::Reset();
		if (Window* window = Application::Get().GetWindow())
			window->SetCursorLocked(false);
		// Selection may point at an entity spawned during play.
		if (m_Selection.IsValid() && !m_EditScene->TryGetEntityByUUID(m_Selection))
			m_Selection = UUID::Invalid;
		PN_CORE_INFO("Play mode stopped");
	}

	Entity EditorLayer::GetSelectedEntity()
	{
		if (!m_Selection.IsValid())
			return {};
		return GetActiveSceneForView()->TryGetEntityByUUID(m_Selection).value_or(Entity{});
	}

	// ------------------------------------------------------------------------------------------
	// Undo / redo: whole-scene snapshots, cheap enough for authored levels and always correct.
	// ------------------------------------------------------------------------------------------

	void EditorLayer::ResetUndoHistory()
	{
		m_UndoStack.clear();
		m_RedoStack.clear();
		m_StableSnapshot = SceneSerializer::SerializeToJson(*m_EditScene);
		m_ChangeOpen = false;
	}

	void EditorLayer::MarkChanged()
	{
		if (m_PlayScene || m_ChangeOpen)
			return;
		m_UndoStack.push_back(m_StableSnapshot);
		while (m_UndoStack.size() > MaxUndoSteps)
			m_UndoStack.pop_front();
		m_RedoStack.clear();
		m_ChangeOpen = true;
	}

	void EditorLayer::CommitPendingChange()
	{
		if (!m_ChangeOpen)
			return;
		m_StableSnapshot = SceneSerializer::SerializeToJson(*m_EditScene);
		m_ChangeOpen = false;
	}

	json EditorLayer::ExecuteRecorded(const json& request)
	{
		CommitPendingChange();
		const std::string command = request.is_object() ? request.value("cmd", std::string()) : std::string();
		static const std::unordered_set<std::string> s_ReadOnly = { "help", "log.tail", "project.info", "scene.get", "scene.list",
			"entity.get", "component.list", "asset.list", "asset.read", "script.errors", "render.screenshot" };
		if (m_PlayScene || s_ReadOnly.contains(command))
			return m_Commands->Execute(request);

		Ref<Scene> before = m_EditScene;
		MarkChanged();
		json response = m_Commands->Execute(request);
		if (m_EditScene != before)
		{
			// The command replaced the scene (scene.open, project.open...): history restarted already.
			return response;
		}
		CommitPendingChange();
		if (!m_UndoStack.empty() && m_UndoStack.back() == m_StableSnapshot)
			m_UndoStack.pop_back(); // nothing actually changed
		return response;
	}

	void EditorLayer::RestoreSnapshot(const json& snapshot)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		std::string error;
		if (!SceneSerializer::DeserializeFromJson(*scene, snapshot, error))
		{
			PN_CORE_ERROR("Undo failed: {}", error);
			return;
		}
		scene->SetFilePath(m_EditScene->GetFilePath());
		m_EditScene = scene;
		m_StableSnapshot = snapshot;
		m_ChangeOpen = false;
		if (m_Selection.IsValid() && !m_EditScene->TryGetEntityByUUID(m_Selection))
			m_Selection = UUID::Invalid;
	}

	void EditorLayer::Undo()
	{
		if (m_PlayScene || m_UndoStack.empty())
			return;
		CommitPendingChange();
		m_RedoStack.push_back(m_StableSnapshot);
		const json snapshot = std::move(m_UndoStack.back());
		m_UndoStack.pop_back();
		RestoreSnapshot(snapshot);
	}

	void EditorLayer::Redo()
	{
		if (m_PlayScene || m_RedoStack.empty())
			return;
		CommitPendingChange();
		m_UndoStack.push_back(m_StableSnapshot);
		const json snapshot = std::move(m_RedoStack.back());
		m_RedoStack.pop_back();
		RestoreSnapshot(snapshot);
	}

	// ------------------------------------------------------------------------------------------
	// Files
	// ------------------------------------------------------------------------------------------

	void EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		const json response = m_Commands->Execute({ { "cmd", "project.open" }, { "path", path.string() } });
		if (!response.value("ok", false))
			PN_CORE_ERROR("Cannot open project: {}", response.value("error", std::string()));
	}

	void EditorLayer::OpenScene(const std::string& path)
	{
		const json response = m_Commands->Execute({ { "cmd", "scene.open" }, { "path", path } });
		if (!response.value("ok", false))
			PN_CORE_ERROR("{}", response.value("error", std::string()));
	}

	void EditorLayer::SaveScene()
	{
		if (m_EditScene->GetFilePath().empty())
		{
			m_PathPopupTitle = "Save Scene As";
			std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "Scenes/NewScene.pscene");
			m_PathPopupAction = [this](const std::string& path) { SaveSceneAs(path); };
			m_OpenPathPopup = true;
			return;
		}
		CommitPendingChange();
		if (SceneSerializer::Serialize(*m_EditScene, m_EditScene->GetFilePath()))
			PN_CORE_INFO("Saved {}", m_EditScene->GetFilePath().string());
	}

	void EditorLayer::SaveSceneAs(const std::string& path)
	{
		CommitPendingChange();
		const json response = m_Commands->Execute({ { "cmd", "scene.save" }, { "path", path } });
		if (!response.value("ok", false))
			PN_CORE_ERROR("{}", response.value("error", std::string()));
	}

	void EditorLayer::ExportGame(const std::string& output)
	{
		const json response = m_Commands->Execute({ { "cmd", "project.export" }, { "output", output } });
		if (response.value("ok", false))
			PN_CORE_INFO("Game exported: {}", response["result"].value("executable", std::string()));
		else
			PN_CORE_ERROR("Export failed: {}", response.value("error", std::string()));
	}

	// ------------------------------------------------------------------------------------------
	// Scene editing helpers
	// ------------------------------------------------------------------------------------------

	Entity EditorLayer::CreatePrimitive(const std::string& name, const std::string& mesh)
	{
		MarkChanged();
		Ref<Scene> scene = GetActiveSceneForView();
		Entity entity = scene->CreateEntity(name);
		entity.Transform().Translation = m_Camera.GetPosition() + Math::Forward(m_Camera.GetRotation()) * 5.0f;
		entity.AddComponent<MeshComponent>().Mesh = mesh;
		entity.AddComponent<MaterialComponent>();
		if (mesh == "builtin:sphere")
			entity.AddComponent<SphereColliderComponent>();
		else if (mesh == "builtin:capsule")
			entity.AddComponent<CapsuleColliderComponent>().HalfHeight = 0.5f;
		else if (mesh == "builtin:plane")
			entity.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.01f, 0.5f };
		else
			entity.AddComponent<BoxColliderComponent>();
		m_Selection = entity.GetUUID();
		return entity;
	}

	Entity EditorLayer::CreateLight(const std::string& type)
	{
		MarkChanged();
		Ref<Scene> scene = GetActiveSceneForView();
		Entity entity = scene->CreateEntity(type);
		entity.Transform().Translation = m_Camera.GetPosition() + Math::Forward(m_Camera.GetRotation()) * 5.0f;
		if (type == "Directional Light")
		{
			entity.Transform().Rotation = Math::LookRotation(glm::vec3(-0.3f, -1.0f, -0.5f));
			entity.AddComponent<DirectionalLightComponent>();
		}
		else if (type == "Spot Light")
		{
			entity.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, -1.0f, 0.0f));
			entity.AddComponent<SpotLightComponent>();
		}
		else
		{
			entity.AddComponent<PointLightComponent>();
		}
		m_Selection = entity.GetUUID();
		return entity;
	}

	void EditorLayer::PickEntity(const glm::vec2& viewportPosition)
	{
		Ref<Scene> scene = GetActiveSceneForView();
		const float aspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 1.0f);
		const RenderCamera camera = m_Camera.GetRenderCamera(aspect);
		const glm::vec2 ndc((viewportPosition.x / m_ViewportSize.x) * 2.0f - 1.0f, 1.0f - (viewportPosition.y / m_ViewportSize.y) * 2.0f);
		const glm::mat4 inverse = glm::inverse(camera.Projection * camera.View);
		glm::vec4 nearPoint = inverse * glm::vec4(ndc, 0.0f, 1.0f);
		glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;
		const glm::vec3 origin(nearPoint);
		const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) - origin);

		UUID best = UUID::Invalid;
		float bestDistance = std::numeric_limits<float>::max();
		for (auto [handle, mesh] : scene->GetRegistry().view<MeshComponent>().each())
		{
			std::shared_ptr<const MeshData> data = AssetManager::GetMesh(mesh.Mesh);
			if (!data)
				continue;
			Entity entity(handle, scene.get());
			if (const std::optional<float> distance = RayHitsBox(origin, direction, data->Bounds, scene->GetWorldTransform(entity)))
			{
				if (*distance < bestDistance)
				{
					bestDistance = *distance;
					best = entity.GetUUID();
				}
			}
		}
		m_Selection = best;
	}

	// ------------------------------------------------------------------------------------------
	// Frame
	// ------------------------------------------------------------------------------------------

	void EditorLayer::OnUpdate(Timestep ts)
	{
		m_FrameTime = ts;
		m_Server.ProcessPending([this](const json& request) { return ExecuteRecorded(request); });

		if (!m_UseSceneCamera)
			m_Camera.Update(ts, m_ViewportHovered, m_ViewportFocused);

		if (m_PlayScene)
		{
			m_PlayScene->OnUpdateRuntime(ts);
			if (std::optional<bool> locked = m_PlayScene->ConsumeCursorLockRequest())
				Application::Get().GetWindow()->SetCursorLocked(*locked && m_ViewportFocused);
			if (m_PlayScene && m_PlayScene->GetRequestedQuit())
				StopPlay();
			if (m_PlayScene)
			{
				if (std::optional<std::string> next = m_PlayScene->GetRequestedScene())
				{
					Ref<Scene> loaded = CreateRef<Scene>();
					std::string error;
					if (SceneSerializer::Deserialize(*loaded, Project::ResolveAssetPath(*next), error))
					{
						m_PlayScene->OnRuntimeStop();
						m_PlayScene = loaded;
						m_PlayScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
						m_PlayScene->SetRandomSeed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
						m_PlayScene->OnRuntimeStart();
					}
					else
					{
						PN_CORE_ERROR("{}", error);
						m_PlayScene->ClearRequestedScene();
					}
				}
			}
		}

		m_HotReloadTimer += ts;
		if (m_HotReloadTimer > 1.0f)
		{
			m_HotReloadTimer = 0.0f;
			if (const size_t reloaded = AssetManager::ReloadModified(); reloaded > 0)
				PN_CORE_INFO("Hot-reloaded {} asset(s)", reloaded);
		}
	}

	void EditorLayer::DrawDebugOverlays(Scene& scene)
	{
		DebugDraw& debug = m_Renderer->GetDebugDraw();
		if (m_ShowGrid && !m_PlayScene)
			debug.Grid(20.0f, 1.0f, { 0.35f, 0.35f, 0.4f, 0.5f });

		if (m_ShowPerception)
		{
			for (auto [handle, perception] : scene.GetRegistry().view<PerceptionComponent>().each())
			{
				Entity entity(handle, &scene);
				const glm::vec3 eye = scene.GetWorldPosition(entity) + glm::vec3(0.0f, perception.EyeHeight, 0.0f);
				const glm::vec3 forward = Math::Forward(scene.GetWorldRotation(entity));
				const glm::vec4 color = StateColor(perception.RuntimeState);
				debug.Cone(eye, forward, std::min(perception.ViewDistance, 6.0f), perception.FieldOfView * 0.5f, color);
				// Suspicion meter above the head.
				const glm::vec3 barStart = eye + glm::vec3(-0.5f, 0.5f, 0.0f);
				debug.Line(barStart, barStart + glm::vec3(1.0f, 0.0f, 0.0f), { 0.2f, 0.2f, 0.2f, 1.0f });
				debug.Line(barStart + glm::vec3(0.0f, 0.03f, 0.0f), barStart + glm::vec3(perception.RuntimeSuspicion, 0.03f, 0.0f), color);
				if (perception.RuntimeHasStimulus)
				{
					debug.Line(eye, perception.RuntimeLastStimulusPosition, { color.r, color.g, color.b, 0.6f });
					debug.Sphere(perception.RuntimeLastStimulusPosition, 0.2f, color);
				}
			}
			for (auto [handle, smoke] : scene.GetRegistry().view<SmokeVolumeComponent>().each())
				debug.Sphere(scene.GetWorldPosition(Entity(handle, &scene)), smoke.Radius, { 0.7f, 0.7f, 0.7f, 0.3f });
		}

		// The navmesh exists while the game runs (it is baked from the physics world).
		if (NavigationSystem* navigation = scene.GetNavigationSystem(); m_ShowNavMesh && navigation)
		{
			if (const NavMesh* navMesh = navigation->GetNavMesh())
			{
				const std::vector<glm::vec3> edges = navMesh->GetDebugEdges();
				const glm::vec3 lift(0.0f, 0.05f, 0.0f);
				for (size_t i = 0; i + 1 < edges.size(); i += 2)
					debug.Line(edges[i] + lift, edges[i + 1] + lift, { 0.2f, 0.6f, 1.0f, 0.5f });
			}
			for (auto [handle, agent] : scene.GetRegistry().view<NavAgentComponent>().each())
			{
				Entity entity(handle, &scene);
				const std::vector<glm::vec3>* path = navigation->GetAgentPath(entity.GetUUID());
				if (!path || path->empty())
					continue;
				glm::vec3 previous = scene.GetWorldPosition(entity) + glm::vec3(0.0f, 0.1f, 0.0f);
				for (const glm::vec3& corner : *path)
				{
					debug.Line(previous, corner + glm::vec3(0.0f, 0.1f, 0.0f), { 1.0f, 0.9f, 0.2f, 1.0f });
					previous = corner + glm::vec3(0.0f, 0.1f, 0.0f);
				}
				debug.Sphere(agent.RuntimeDestination, 0.15f, { 1.0f, 0.9f, 0.2f, 1.0f });
			}
		}

		if (m_ShowColliders)
		{
			const glm::vec4 color(0.3f, 1.0f, 0.5f, 0.8f);
			for (auto [handle, box] : scene.GetRegistry().view<BoxColliderComponent>().each())
			{
				Entity entity(handle, &scene);
				glm::vec3 translation, scale;
				glm::quat rotation;
				Math::DecomposeTransform(scene.GetWorldTransform(entity), translation, rotation, scale);
				debug.Box(translation + rotation * (box.Offset * scale), box.HalfExtents * glm::abs(scale), rotation, color);
			}
			for (auto [handle, sphere] : scene.GetRegistry().view<SphereColliderComponent>().each())
			{
				Entity entity(handle, &scene);
				const glm::mat4 world = scene.GetWorldTransform(entity);
				const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
				debug.Sphere(glm::vec3(world * glm::vec4(sphere.Offset, 1.0f)), sphere.Radius * std::max({ scale.x, scale.y, scale.z }), color);
			}
		}

		if (Entity selected = GetSelectedEntity(); selected && !m_PlayScene)
		{
			if (const MeshComponent* mesh = selected.TryGetComponent<MeshComponent>())
			{
				if (std::shared_ptr<const MeshData> data = AssetManager::GetMesh(mesh->Mesh))
				{
					glm::vec3 translation, scale;
					glm::quat rotation;
					Math::DecomposeTransform(scene.GetWorldTransform(selected), translation, rotation, scale);
					debug.Box(translation + rotation * (data->Bounds.GetCenter() * scale), data->Bounds.GetExtents() * glm::abs(scale) * 1.02f, rotation,
						{ 1.0f, 0.6f, 0.1f, 1.0f });
				}
			}
		}
	}

	void EditorLayer::OnRender()
	{
		Ref<Scene> scene = GetActiveSceneForView();
		const uint32_t width = static_cast<uint32_t>(std::max(m_ViewportSize.x, 1.0f));
		const uint32_t height = static_cast<uint32_t>(std::max(m_ViewportSize.y, 1.0f));
		m_Renderer->SetViewportSize(width, height);
		scene->OnViewportResize(width, height);

		DrawDebugOverlays(*scene);
		const float aspect = static_cast<float>(width) / static_cast<float>(height);
		const RenderCamera camera = m_UseSceneCamera ? RenderCamera::FromScene(*scene, aspect) : m_Camera.GetRenderCamera(aspect);
		m_Renderer->Render(*scene, camera);

		m_ImGui->BeginFrame();
		ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
		DrawMenuBar();
		DrawToolbar();
		DrawHierarchy();
		const bool propertiesChanged = [&] {
			// Snapshot before the first modification of an interaction.
			Entity selected = GetSelectedEntity();
			return m_Properties.Draw(selected, m_PlayScene != nullptr);
		}();
		if (propertiesChanged)
		{
			// Properties write into the entity immediately, so the pre-edit snapshot is the stable one.
			MarkChanged();
		}
		DrawContentBrowser();
		DrawConsole();
		DrawStatistics();
		DrawViewport();
		DrawPathPopup();

		if (m_ChangeOpen && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing())
			CommitPendingChange();

		m_ImGui->EndFrame(Application::Get().GetRenderDevice().GetBackBufferFramebuffer());
	}

	void EditorLayer::OnEvent(Event& event)
	{
		if (event.Type != EventType::KeyPressed || event.Repeat)
			return;
		const bool control = Input::IsKeyDown(Key::LeftControl) || Input::IsKeyDown(Key::RightControl);

		if (event.Key == Key::Escape && m_PlayScene)
			Application::Get().GetWindow()->SetCursorLocked(false);
		if (event.Key == Key::F5)
			m_PlayScene ? StopPlay() : static_cast<void>(StartPlay());

		if (ImGui::GetIO().WantTextInput)
			return; // typing in a text field: shortcuts belong to the field

		if (control)
		{
			if (event.Key == Key::S)
				SaveScene();
			else if (event.Key == Key::Z)
				Undo();
			else if (event.Key == Key::Y)
				Redo();
			else if (event.Key == Key::D)
			{
				if (Entity selected = GetSelectedEntity())
				{
					MarkChanged();
					m_Selection = GetActiveSceneForView()->DuplicateEntity(selected).GetUUID();
				}
			}
			return;
		}

		if (m_ImGui->WantsKeyboard() && !m_ViewportFocused)
			return;
		if (Input::IsMouseButtonDown(Mouse::ButtonRight))
			return; // flying: WASD/QE belong to the camera

		switch (event.Key)
		{
			case Key::W: m_GizmoOperation = ImGuizmo::TRANSLATE; break;
			case Key::E: m_GizmoOperation = ImGuizmo::ROTATE; break;
			case Key::R: m_GizmoOperation = ImGuizmo::SCALE; break;
			case Key::F:
				if (Entity selected = GetSelectedEntity())
					m_Camera.Focus(GetActiveSceneForView()->GetWorldPosition(selected), 6.0f);
				break;
			case Key::Delete:
				if (Entity selected = GetSelectedEntity())
				{
					MarkChanged();
					GetActiveSceneForView()->DestroyEntity(selected);
					m_Selection = UUID::Invalid;
				}
				break;
			default: break;
		}
	}

	// ------------------------------------------------------------------------------------------
	// Panels
	// ------------------------------------------------------------------------------------------

	void EditorLayer::DrawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
				SetEditScene(CreateRef<Scene>());
			if (ImGui::MenuItem("Open Scene..."))
			{
				m_PathPopupTitle = "Open Scene";
				std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "Scenes/");
				m_PathPopupAction = [this](const std::string& path) { OpenScene(path); };
				m_OpenPathPopup = true;
			}
			if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
				SaveScene();
			if (ImGui::MenuItem("Save Scene As..."))
			{
				m_PathPopupTitle = "Save Scene As";
				std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "Scenes/NewScene.pscene");
				m_PathPopupAction = [this](const std::string& path) { SaveSceneAs(path); };
				m_OpenPathPopup = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Open Project..."))
			{
				m_PathPopupTitle = "Open Project (folder or project.json)";
				m_PathBuffer[0] = '\0';
				m_PathPopupAction = [this](const std::string& path) { OpenProject(path); };
				m_OpenPathPopup = true;
			}
			if (ImGui::MenuItem("Export Game...", nullptr, false, Project::IsLoaded()))
			{
				m_PathPopupTitle = "Export Game To Folder";
				std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "%s", (Project::GetProjectDirectory().parent_path() / "Export").string().c_str());
				m_PathPopupAction = [this](const std::string& path) { ExportGame(path); };
				m_OpenPathPopup = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
				Application::Get().Close();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_UndoStack.empty() && !m_PlayScene))
				Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_RedoStack.empty() && !m_PlayScene))
				Redo();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Empty"))
			{
				MarkChanged();
				m_Selection = GetActiveSceneForView()->CreateEntity("Entity").GetUUID();
			}
			for (const char* primitive : { "Cube", "Sphere", "Plane", "Capsule", "Cylinder" })
			{
				if (ImGui::MenuItem(primitive))
				{
					std::string mesh = std::string("builtin:") + primitive;
					std::transform(mesh.begin(), mesh.end(), mesh.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
					CreatePrimitive(primitive, mesh);
				}
			}
			ImGui::Separator();
			for (const char* light : { "Directional Light", "Point Light", "Spot Light" })
			{
				if (ImGui::MenuItem(light))
					CreateLight(light);
			}
			if (ImGui::MenuItem("Camera"))
			{
				MarkChanged();
				Entity camera = GetActiveSceneForView()->CreateEntity("Camera");
				camera.Transform().Translation = m_Camera.GetPosition();
				camera.Transform().Rotation = m_Camera.GetRotation();
				camera.AddComponent<CameraComponent>();
				m_Selection = camera.GetUUID();
			}
			if (ImGui::MenuItem("Environment"))
			{
				MarkChanged();
				Entity environment = GetActiveSceneForView()->CreateEntity("Environment");
				environment.AddComponent<EnvironmentComponent>();
				m_Selection = environment.GetUUID();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Grid", nullptr, &m_ShowGrid);
			ImGui::MenuItem("Colliders", nullptr, &m_ShowColliders);
			ImGui::MenuItem("Perception debug", nullptr, &m_ShowPerception);
			ImGui::MenuItem("Navmesh (play mode)", nullptr, &m_ShowNavMesh);
			ImGui::MenuItem("Game camera", nullptr, &m_UseSceneCamera);
			ImGui::EndMenu();
		}
		ImGui::TextDisabled("   %s", m_Server.IsRunning() ? std::format("automation: 127.0.0.1:{}", m_Server.GetPort()).c_str() : "automation: off");
		ImGui::EndMainMenuBar();
	}

	void EditorLayer::DrawToolbar()
	{
		ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);
		if (!m_PlayScene)
		{
			if (ImGui::Button("Play (F5)"))
				StartPlay();
		}
		else
		{
			if (ImGui::Button("Stop (F5)"))
				StopPlay();
			ImGui::SameLine();
			bool paused = m_PlayScene && m_PlayScene->IsPaused();
			if (ImGui::Checkbox("Pause", &paused) && m_PlayScene)
				m_PlayScene->SetPaused(paused);
			ImGui::SameLine();
			if (ImGui::Button("Step") && m_PlayScene)
				m_PlayScene->Step(1);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::RadioButton("Move (W)", &m_GizmoOperation, ImGuizmo::TRANSLATE);
		ImGui::SameLine();
		ImGui::RadioButton("Rotate (E)", &m_GizmoOperation, ImGuizmo::ROTATE);
		ImGui::SameLine();
		ImGui::RadioButton("Scale (R)", &m_GizmoOperation, ImGuizmo::SCALE);
		ImGui::SameLine();
		ImGui::Checkbox("Local", &m_GizmoLocal);
		ImGui::End();
	}

	void EditorLayer::DrawHierarchyNode(Entity entity)
	{
		const std::vector<Entity> children = entity.GetChildren();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (entity.GetUUID() == m_Selection)
			flags |= ImGuiTreeNodeFlags_Selected;

		const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint64_t>(entity.GetUUID()))), flags, "%s",
			entity.GetName().c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			m_Selection = entity.GetUUID();

		if (ImGui::BeginDragDropSource())
		{
			const uint64_t id = entity.GetUUID();
			ImGui::SetDragDropPayload("PENUMBRA_ENTITY", &id, sizeof(id));
			ImGui::TextUnformatted(entity.GetName().c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PENUMBRA_ENTITY"))
			{
				const UUID dropped(*static_cast<const uint64_t*>(payload->Data));
				if (std::optional<Entity> child = entity.GetScene()->TryGetEntityByUUID(dropped); child && *child != entity)
				{
					MarkChanged();
					entity.GetScene()->SetParent(*child, entity, true);
				}
			}
			ImGui::EndDragDropTarget();
		}

		bool destroy = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Duplicate"))
			{
				MarkChanged();
				m_Selection = entity.GetScene()->DuplicateEntity(entity).GetUUID();
			}
			if (ImGui::MenuItem("Unparent", nullptr, false, entity.GetParent().IsValid()))
			{
				MarkChanged();
				entity.GetScene()->Unparent(entity, true);
			}
			if (ImGui::MenuItem("Save as Prefab...", nullptr, false, Project::IsLoaded()))
			{
				const UUID id = entity.GetUUID();
				m_PathPopupTitle = "Save Prefab";
				std::snprintf(m_PathBuffer, sizeof(m_PathBuffer), "Prefabs/%s.pprefab", entity.GetName().c_str());
				m_PathPopupAction = [this, id](const std::string& path) {
					const json response = m_Commands->Execute({ { "cmd", "prefab.save" }, { "id", std::to_string(static_cast<uint64_t>(id)) }, { "path", path } });
					if (!response.value("ok", false))
						PN_CORE_ERROR("{}", response.value("error", std::string()));
				};
				m_OpenPathPopup = true;
			}
			if (ImGui::MenuItem("Delete"))
				destroy = true;
			ImGui::EndPopup();
		}

		if (open)
		{
			for (Entity child : children)
			{
				if (child.IsValid())
					DrawHierarchyNode(child);
			}
			ImGui::TreePop();
		}

		if (destroy)
		{
			MarkChanged();
			if (entity.GetUUID() == m_Selection)
				m_Selection = UUID::Invalid;
			entity.GetScene()->DestroyEntity(entity);
		}
	}

	void EditorLayer::DrawHierarchy()
	{
		ImGui::Begin("Hierarchy");
		Ref<Scene> scene = GetActiveSceneForView();
		ImGui::TextDisabled("%s (%zu entities)", m_PlayScene ? "PLAYING" : "Edit scene", scene->GetEntityCount());
		for (Entity root : scene->GetRootEntities())
		{
			if (root.IsValid())
				DrawHierarchyNode(root);
		}
		// Dropping on empty space un-parents.
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 20.0f)));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PENUMBRA_ENTITY"))
			{
				if (std::optional<Entity> child = scene->TryGetEntityByUUID(UUID(*static_cast<const uint64_t*>(payload->Data))))
				{
					MarkChanged();
					scene->Unparent(*child, true);
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
			m_Selection = UUID::Invalid;
		ImGui::End();
	}

	void EditorLayer::DrawContentBrowser()
	{
		ImGui::Begin("Content");
		if (!Project::IsLoaded())
		{
			ImGui::TextDisabled("No project loaded (File > Open Project)");
			ImGui::End();
			return;
		}

		const std::filesystem::path root = Project::GetAssetDirectory();
		std::function<void(const std::filesystem::path&)> drawDirectory = [&](const std::filesystem::path& directory) {
			std::vector<std::filesystem::directory_entry> entries;
			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator(directory, error))
				entries.push_back(entry);
			std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
				if (a.is_directory() != b.is_directory())
					return a.is_directory();
				return a.path().filename() < b.path().filename();
			});
			for (const auto& entry : entries)
			{
				const std::string name = entry.path().filename().string();
				if (entry.is_directory())
				{
					if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
					{
						drawDirectory(entry.path());
						ImGui::TreePop();
					}
					continue;
				}
				const std::string relative = Project::MakeAssetRelative(entry.path());
				ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("PENUMBRA_ASSET", relative.c_str(), relative.size() + 1);
					ImGui::TextUnformatted(relative.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					const std::string extension = entry.path().extension().string();
					if (extension == ".pscene")
						OpenScene(relative);
					else
						ShellExecuteW(nullptr, L"open", entry.path().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}
			}
		};
		drawDirectory(root);
		ImGui::End();
	}

	void EditorLayer::DrawConsole()
	{
		ImGui::Begin("Console");
		if (ImGui::SmallButton("Clear"))
		{
			std::scoped_lock lock(m_ConsoleMutex);
			m_Console.clear();
		}
		ImGui::SameLine();
		ImGui::TextDisabled(m_PlayScene ? "Input runs Lua in the playing scene" : "Input runs an automation command (JSON)");
		ImGui::BeginChild("ConsoleScroll", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()));
		{
			std::scoped_lock lock(m_ConsoleMutex);
			for (const ConsoleLine& line : m_Console)
			{
				ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
				if (line.Level == LogLevel::Warn)
					color = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
				else if (line.Level >= LogLevel::Error)
					color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
				else if (line.Level == LogLevel::Trace)
					color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
				ImGui::TextColored(color, "%s", line.Text.c_str());
			}
		}
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		if (ImGui::InputText("##ConsoleInput", m_ConsoleInput, sizeof(m_ConsoleInput), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			const std::string input = m_ConsoleInput;
			m_ConsoleInput[0] = '\0';
			if (m_PlayScene && m_PlayScene->GetScriptEngine())
			{
				std::string output;
				if (m_PlayScene->GetScriptEngine()->Execute(input, output))
					PN_CORE_INFO("> {}{}", input, output.empty() ? "" : " = " + output);
				else
					PN_CORE_ERROR("> {}: {}", input, output);
			}
			else
			{
				PN_CORE_INFO("{}", ExecuteRecorded(json::parse(input, nullptr, false)).dump());
			}
			ImGui::SetKeyboardFocusHere(-1);
		}
		ImGui::End();
	}

	void EditorLayer::DrawStatistics()
	{
		ImGui::Begin("Statistics");
		const SceneRendererStatistics& stats = m_Renderer->GetStatistics();
		ImGui::Text("Frame: %.2f ms (%.0f FPS)", m_FrameTime * 1000.0f, m_FrameTime > 0.0f ? 1.0f / m_FrameTime : 0.0f);
		ImGui::Text("GPU: %s", Application::Get().GetRenderDevice().GetAdapterName().c_str());
		ImGui::Text("Draw calls: %u  Triangles: %u", stats.DrawCalls, stats.Triangles);
		ImGui::Text("Lights: %u  Shadow views: %u  Culled: %u", stats.Lights, stats.ShadowViews, stats.CulledObjects);
		if (m_PlayScene)
		{
			ImGui::Separator();
			ImGui::Text("Time: %.2f s  Frame: %llu", m_PlayScene->GetTime(), static_cast<unsigned long long>(m_PlayScene->GetFrameCount()));
			for (auto [handle, target] : m_PlayScene->GetRegistry().view<PerceptionTargetComponent>().each())
			{
				Entity entity(handle, m_PlayScene.get());
				ImGui::Text("%s: light %.3f  exposure %.2f", entity.GetName().c_str(), target.RuntimeLightLevel, target.RuntimeExposure);
			}
			for (auto [handle, perception] : m_PlayScene->GetRegistry().view<PerceptionComponent>().each())
			{
				Entity entity(handle, m_PlayScene.get());
				ImGui::Text("%s: %s  D=%.2f  B=%.2f  La=%.3f", entity.GetName().c_str(), std::string(EnumToString(perception.RuntimeState)).c_str(),
					perception.RuntimeSuspicion, perception.RuntimeStimulus, perception.RuntimeAdaptation);
			}
		}
		ImGui::End();
	}

	void EditorLayer::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");
		const ImVec2 available = ImGui::GetContentRegionAvail();
		m_ViewportSize = glm::vec2(std::max(available.x, 16.0f), std::max(available.y, 16.0f));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		m_ViewportOrigin = glm::vec2(origin.x, origin.y);
		m_ViewportHovered = ImGui::IsWindowHovered();
		m_ViewportFocused = ImGui::IsWindowFocused();

		ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(m_Renderer->GetOutputTexture()))), available);

		// Asset drops: prefabs and meshes spawn in front of the camera, HDRIs become the environment.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PENUMBRA_ASSET"))
			{
				const std::string path(static_cast<const char*>(payload->Data));
				const std::string extension = std::filesystem::path(path).extension().string();
				const glm::vec3 spawn = m_Camera.GetPosition() + Math::Forward(m_Camera.GetRotation()) * 6.0f;
				Ref<Scene> scene = GetActiveSceneForView();
				MarkChanged();
				if (extension == ".pprefab")
				{
					if (Entity root = Prefab::Instantiate(*scene, path, spawn))
						m_Selection = root.GetUUID();
				}
				else if (extension == ".gltf" || extension == ".glb")
				{
					Entity entity = scene->CreateEntity(std::filesystem::path(path).stem().string());
					entity.Transform().Translation = spawn;
					entity.AddComponent<MeshComponent>().Mesh = path;
					entity.AddComponent<MeshColliderComponent>();
					m_Selection = entity.GetUUID();
				}
				else if (extension == ".hdr")
				{
					Entity environment = scene->GetEnvironmentEntity();
					if (!environment)
					{
						environment = scene->CreateEntity("Environment");
						environment.AddComponent<EnvironmentComponent>();
					}
					environment.GetComponent<EnvironmentComponent>().EnvironmentMap = path;
				}
				else if (extension == ".pscene")
				{
					OpenScene(path);
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Gizmo
		Entity selected = GetSelectedEntity();
		if (selected && !m_UseSceneCamera)
		{
			Ref<Scene> scene = GetActiveSceneForView();
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(origin.x, origin.y, available.x, available.y);
			const RenderCamera camera = m_Camera.GetRenderCamera(m_ViewportSize.x / m_ViewportSize.y);
			glm::mat4 transform = scene->GetWorldTransform(selected);
			const bool snap = Input::IsKeyDown(Key::LeftControl);
			const float snapValue = m_GizmoOperation == ImGuizmo::ROTATE ? 15.0f : 0.5f;
			const float snapValues[3] = { snapValue, snapValue, snapValue };
			if (ImGuizmo::Manipulate(glm::value_ptr(camera.View), glm::value_ptr(camera.Projection), static_cast<ImGuizmo::OPERATION>(m_GizmoOperation),
					m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(transform), nullptr, snap ? snapValues : nullptr))
			{
				MarkChanged();
				scene->SetWorldTransform(selected, transform);
			}
		}

		// Click to select (not while using the gizmo or flying).
		if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !Input::IsKeyDown(Key::LeftAlt) &&
			!m_UseSceneCamera)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			PickEntity(glm::vec2(mouse.x - origin.x, mouse.y - origin.y));
		}
		if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_PlayScene)
			ImGui::SetWindowFocus();

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::DrawPathPopup()
	{
		if (m_OpenPathPopup)
		{
			ImGui::OpenPopup("PathPopup");
			m_OpenPathPopup = false;
		}
		if (!ImGui::BeginPopupModal("PathPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		ImGui::TextUnformatted(m_PathPopupTitle.c_str());
		ImGui::SetNextItemWidth(500.0f);
		const bool entered = ImGui::InputText("##Path", m_PathBuffer, sizeof(m_PathBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere(-1);
		if (ImGui::Button("OK") || entered)
		{
			if (m_PathPopupAction)
				m_PathPopupAction(m_PathBuffer);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

}
