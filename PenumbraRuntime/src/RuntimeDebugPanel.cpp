#include "RuntimeDebugPanel.h"

#include "Penumbra/AI/FogOfWarSystem.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Core/Log.h"
#include "Penumbra/Core/Platform.h"
#include "Penumbra/Debug/ObjectInspector.h"
#include "Penumbra/ImGui/ComponentInspector.h"
#include "Penumbra/ImGui/ImGuiHost.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scene/JsonSerialization.h"
#include "Penumbra/Scene/Scene.h"
#include "Penumbra/Scripting/ScriptEngine.h"
#include "Penumbra/UI/Overlay.h"

#include <chrono>
#include <format>

namespace Penumbra {

	namespace {
		double WallSeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::string Megabytes(uint64_t bytes) { return std::format("{:.0f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0)); }

		ImVec4 ToImVec4(const glm::vec4& color) { return ImVec4(color.r, color.g, color.b, color.a); }

		std::string_view StateName(AwarenessState state)
		{
			for (const auto& [value, name] : EnumNames<AwarenessState>::Values)
			{
				if (value == state)
					return name;
			}
			return "?";
		}

		std::optional<Entity> FindThief(Scene& scene)
		{
			for (auto [handle, target] : scene.GetRegistry().view<PerceptionTargetComponent>().each())
			{
				if (target.Kind == TargetKind::Intruder)
					return Entity(handle, &scene);
			}
			return std::nullopt;
		}

		// One "name  value" row of the performance table.
		void Row(const char* name, const std::string& value)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", name);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(value.c_str());
		}
	}

	RuntimeDebugPanel::RuntimeDebugPanel(GLFWwindow* window, nvrhi::IDevice* device)
	{
		m_ImGui = CreateScope<ImGuiHost>(window, device, nullptr);
		// Arrow keys and Space belong to the game, not to ImGui's keyboard navigation.
		ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
		m_BaseStyle = ImGui::GetStyle();
		m_PhysicalMemory = Platform::GetPhysicalMemoryBytes();
	}

	RuntimeDebugPanel::~RuntimeDebugPanel()
	{
		Input::SetGameInputBlocked(false);
	}

	void RuntimeDebugPanel::Open(std::string_view tools)
	{
		size_t start = 0;
		while (start < tools.size())
		{
			const size_t end = std::min(tools.find(',', start), tools.size());
			const std::string_view name = tools.substr(start, end - start);
			if (name == "performance")
				m_ShowPerformance = true;
			else if (name == "panel")
				m_PanelOpen = true;
			else if (name == "freelook")
				m_FreeLook = true;
			else if (!name.empty())
				PN_CORE_WARN("--debug-ui: unknown tool '{}' (performance, panel, freelook)", name);
			start = end + 1;
		}
		Input::SetGameInputBlocked(BlocksGameInput());
	}

	bool RuntimeDebugPanel::OnKeyPressed(KeyCode key, Scene* scene)
	{
		switch (key)
		{
			case Key::F3:
				m_ShowPerformance = !m_ShowPerformance;
				return true;
			case Key::F4:
				m_PanelOpen = !m_PanelOpen;
				break;
			case Key::F5:
				m_FreeLook = !m_FreeLook;
				m_FreeLookStarted = false; // starts again from the game camera
				break;
			case Key::F6:
				if (scene)
					SetPaused(*scene, !scene->IsPaused());
				return true;
			case Key::F7:
				m_FogOfWar = !m_FogOfWar;
				if (FogOfWarSystem* fog = scene ? scene->GetFogOfWarSystem() : nullptr)
					fog->SetForcedOff(!m_FogOfWar);
				return true;
			default:
				return false;
		}
		Input::SetGameInputBlocked(BlocksGameInput());
		return true;
	}

	void RuntimeDebugPanel::OnSceneLoaded(Scene& scene)
	{
		if (PerceptionSystem* perception = scene.GetPerceptionSystem())
		{
			perception->SetIgnoreIntruders(m_IgnoreThief);
			perception->SetFogGlow(m_FogGlow);
		}
		if (FogOfWarSystem* fog = scene.GetFogOfWarSystem())
			fog->SetForcedOff(!m_FogOfWar);
		m_Noises.clear();
		m_NoiseLog.clear();
		m_LastHearing.clear();
		m_LastNoisePosition.reset();
		m_LastNoiseFrame = ~0ull;
		m_Selection = UUID::Invalid;
		m_Placing = false;
	}

	std::optional<bool> RuntimeDebugPanel::GetCursorOverride() const
	{
		if (m_FreeLook && m_Looking)
			return true;
		if (m_PanelOpen || m_FreeLook)
			return false;
		return std::nullopt;
	}

	void RuntimeDebugPanel::SetPaused(Scene& scene, bool paused) { scene.SetPaused(paused); }

	void RuntimeDebugPanel::AfterUpdate(Scene& scene, float frameMs, float updateMs)
	{
		m_PendingSample = {};
		m_PendingSample.FrameMs = frameMs;
		m_PendingSample.UpdateMs = updateMs;
		m_PendingSample.Systems = scene.GetFrameProfile();

		// Noises made this frame (once per simulated frame: a paused scene keeps its last ones).
		if (scene.GetFrameCount() == m_LastNoiseFrame)
			return;
		m_LastNoiseFrame = scene.GetFrameCount();
		if (PerceptionSystem* perception = scene.GetPerceptionSystem())
		{
			const float now = static_cast<float>(ImGui::GetTime());
			for (const NoiseEvent& noise : perception->GetRecentNoises())
			{
				if (m_ShowAllNoises && noise.Cause != "Debug") // test noises got their ring when clicked
					m_Noises.push_back({ noise.Position, noise.Loudness, now });
				m_NoiseLog.push_front({ scene.GetTime(), noise.Loudness, noise.Cause.empty() ? "Noise" : noise.Cause });
				if (m_NoiseLog.size() > 12)
					m_NoiseLog.pop_back();
			}
			std::erase_if(m_Noises, [now](const DebugNoise& noise) { return now - noise.Time > NoiseLifetime; });
		}
	}

	void RuntimeDebugPanel::UpdateStyle(float viewportHeight)
	{
		// The same rule as the game's HUD (1080 lines = 1), times the user's choice.
		const float scale = OverlayScaleFor(static_cast<uint32_t>(viewportHeight)) * m_UserScale;
		if (scale == m_AppliedScale)
			return;
		m_AppliedScale = scale;
		ImGuiStyle& style = ImGui::GetStyle();
		style = m_BaseStyle;
		style.ScaleAllSizes(scale);
		style.FontScaleMain = scale;
	}

	RenderCamera RuntimeDebugPanel::BeginFrame(Scene& scene, SceneRenderer& renderer, float aspect)
	{
		UpdateStyle(static_cast<float>(renderer.GetViewportHeight()));
		m_ImGui->BeginFrame();
		ImGuiIO& io = ImGui::GetIO();

		m_LastSceneCamera = RenderCamera::FromScene(scene, aspect);
		RenderCamera camera = m_LastSceneCamera;
		if (!m_FreeLook || !ImGui::IsMouseDown(ImGuiMouseButton_Right))
			m_Looking = false;
		if (m_FreeLook)
		{
			if (!m_FreeLookStarted)
			{
				m_FreeCamera.StartFrom(m_LastSceneCamera);
				m_FreeLookStarted = true;
			}
			FreeCameraInput input;
			if (!io.WantCaptureKeyboard)
			{
				auto axis = [](ImGuiKey positive, ImGuiKey negative) {
					return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
				};
				input.Move = glm::vec3(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S));
				input.Fast = io.KeyShift;
			}
			// Hold the right mouse button to look (pressed over the world, not over a panel).
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
				m_Looking = true;
			if (m_Looking)
				input.Look = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
			if (!io.WantCaptureMouse)
				input.SpeedSteps = io.MouseWheel;
			m_FreeCamera.Update(input, io.DeltaTime);
			camera = m_FreeCamera.GetRenderCamera(aspect);
		}

		DebugDraw& debug = renderer.GetDebugDraw();
		if (m_PanelOpen || m_FreeLook)
		{
			Entity selected = m_PanelOpen ? GetSelection(scene) : Entity{};
			if (m_ShowVision)
			{
				const bool onlySelected = m_OnlySelectedVision && selected && selected.HasComponent<PerceptionComponent>();
				DebugOverlays::DrawVision(scene, debug, onlySelected ? selected.GetUUID() : UUID(UUID::Invalid));
				DebugOverlays::DrawSmoke(scene, debug);
			}
			if (m_ShowNoises)
				DebugOverlays::DrawNoises(debug, m_Noises, static_cast<float>(ImGui::GetTime()), NoiseLifetime);
			if (m_ShowNavMesh)
				DebugOverlays::DrawNavigation(scene, debug);
			if (m_ShowColliders)
				DebugOverlays::DrawColliders(scene, debug);
			// The selection: a box round its meshes (a lamp without one: a ball at the light).
			if (selected)
			{
				const glm::vec4 color(1.0f, 0.85f, 0.2f, 1.0f);
				const BoundingBox bounds = ObjectInspector::WorldBounds(scene, selected);
				if (bounds.IsValid())
					debug.Box(bounds.GetCenter(), bounds.GetExtents() + glm::vec3(0.03f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), color);
				else
					debug.Sphere(scene.GetWorldPosition(selected), 0.3f, color);
			}
		}
		return camera;
	}

	void RuntimeDebugPanel::EndFrame(Scene& scene, SceneRenderer& renderer, RenderDevice& device, const RenderCamera& camera,
		nvrhi::IFramebuffer* target, float renderMs)
	{
		m_PendingSample.RenderMs = renderMs;
		m_PendingSample.GpuMs = renderer.GetGpuTimeMs();
		m_Monitor.AddFrame(m_PendingSample, WallSeconds(), Platform::GetProcessUsage());

		if (m_ShowPerformance)
			DrawPerformance(scene, renderer, device);
		if (m_PanelOpen || m_FreeLook)
		{
			if (m_ShowLabels)
				DrawGuardLabels(scene, camera);
			HandleWorldClick(scene, camera);
		}
		if (m_PanelOpen)
		{
			DrawPanel(scene);
			DrawInspector(scene);
			DrawObjectMenu(scene);
		}
		else if (m_FreeLook)
		{
			ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 10.0f * m_AppliedScale), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
			ImGui::SetNextWindowBgAlpha(0.6f);
			ImGui::Begin("##freelook", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
			ImGui::Text("FREELOOK (F5)  RMB look, WASD move, Q/E down/up, Shift fast, wheel %.1f m/s   F4 panel", m_FreeCamera.GetSpeed());
			ImGui::End();
		}

		m_ImGui->EndFrame(target);
	}

	void RuntimeDebugPanel::DrawPerformance(Scene& scene, SceneRenderer& renderer, RenderDevice& device)
	{
		const PerformanceMonitor::Summary& summary = m_Monitor.GetSummary();
		// Video memory is a driver call: twice a second is plenty.
		const double now = WallSeconds();
		if (now - m_VideoMemoryTime > PerformanceMonitor::RefreshInterval)
		{
			m_VideoMemoryTime = now;
			m_HasVideoMemory = device.QueryVideoMemory(m_VideoMemory);
		}

		const float margin = 10.0f * m_AppliedScale;
		ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.72f);
		ImGui::Begin("##performance", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

		const ImVec4 fpsColor = summary.Fps >= 55.0f ? ImVec4(0.45f, 0.95f, 0.5f, 1.0f)
								: summary.Fps >= 30.0f ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f)
													   : ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
		ImGui::TextColored(fpsColor, "%.0f FPS", summary.Fps);
		ImGui::SameLine();
		ImGui::TextDisabled("frame %.2f ms (min %.2f, max %.2f)   F3", summary.FrameMsAverage, summary.FrameMsMin, summary.FrameMsMax);

		// Frame times of the last few seconds; the line marks 60 FPS.
		const auto& history = m_Monitor.GetFrameHistory();
		const float graphMax = std::max(33.4f, summary.FrameMsMax * 1.2f);
		const ImVec2 graphSize(300.0f * m_AppliedScale, 46.0f * m_AppliedScale);
		ImGui::PlotLines("##frames", history.data(), static_cast<int>(history.size()), static_cast<int>(m_Monitor.GetHistoryOffset()), nullptr, 0.0f,
			graphMax, graphSize);
		const ImVec2 graphMin = ImGui::GetItemRectMin();
		const float y60 = graphMin.y + graphSize.y * (1.0f - 16.667f / graphMax);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(graphMin.x, y60), ImVec2(graphMin.x + graphSize.x, y60), IM_COL32(120, 200, 255, 90));

		if (ImGui::BeginTable("##perf", 2, ImGuiTableFlags_SizingFixedFit))
		{
			Row("CPU", std::format("{:.0f}% of a core ({:.1f}% of {} threads)", summary.CpuCorePercent, summary.CpuMachinePercent,
						   Platform::GetLogicalCoreCount()));
			Row("  update", std::format("{:.2f} ms", summary.UpdateMsAverage));
			Row("  render", std::format("{:.2f} ms (CPU side)", summary.RenderMsAverage));
			Row("GPU", summary.GpuMsAverage ? std::format("{:.2f} ms per frame", *summary.GpuMsAverage) : std::string("no timer"));
			if (m_HasVideoMemory)
				Row("  memory", m_VideoMemory.HasUsage ? std::format("{} of {}", Megabytes(m_VideoMemory.UsedBytes), Megabytes(m_VideoMemory.BudgetBytes))
													   : std::format("? of {}", Megabytes(m_VideoMemory.BudgetBytes)));
			Row("  adapter", device.GetAdapterName() + (device.GetDriverName().empty() ? "" : " (" + device.GetDriverName() + ")"));
			Row("RAM", std::format("{} (peak {}){}", Megabytes(summary.ResidentBytes), Megabytes(summary.PeakResidentBytes),
						   m_PhysicalMemory ? std::format(" of {:.0f} GB", static_cast<double>(m_PhysicalMemory) / (1024.0 * 1024.0 * 1024.0))
											: std::string()));
			const SceneRendererStatistics& stats = renderer.GetStatistics();
			Row("draws", std::format("{} calls, {:.1f}k triangles, {} lights, {} shadow views, {} culled", stats.DrawCalls,
							 static_cast<double>(stats.Triangles) / 1000.0, stats.Lights, stats.ShadowViews, stats.CulledObjects));
			const PhysicsWorld* physics = scene.GetPhysicsWorld();
			Row("scene", std::format("{} entities, {} physics bodies{}", scene.GetEntityCount(), physics ? physics->GetBodyCount() : 0,
							 scene.IsPaused() ? ", PAUSED" : ""));
			ImGui::EndTable();
		}

		// Where the simulation's time goes.
		const SceneFrameProfile& systems = summary.Systems;
		if (ImGui::BeginTable("##systems", 4, ImGuiTableFlags_SizingFixedFit))
		{
			const std::pair<const char*, float> entries[] = {
				{ "scripts", systems.Scripts }, { "navigation", systems.Navigation }, { "physics", systems.Physics },
				{ "contacts", systems.Contacts }, { "stims", systems.Stims }, { "perception", systems.Perception },
				{ "smoke", systems.Smoke }, { "audio", systems.Audio }, { "ragdolls", systems.Ragdolls },
				{ "animation", systems.Animation }, { "fog of war", systems.FogOfWar }, { "destroy", systems.Destruction }, { "total", systems.Total() },
			};
			for (size_t i = 0; i < std::size(entries); ++i)
			{
				if (i % 2 == 0)
					ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", entries[i].first);
				ImGui::TableNextColumn();
				ImGui::Text("%.2f ms", entries[i].second);
			}
			ImGui::EndTable();
		}
		ImGui::End();
	}

	void RuntimeDebugPanel::DrawPanel(Scene& scene)
	{
		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f * m_AppliedScale, 10.0f * m_AppliedScale), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(430.0f * m_AppliedScale, 0.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Debug (F4)", &m_PanelOpen, ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			Input::SetGameInputBlocked(BlocksGameInput());
			return;
		}

		ImGui::TextDisabled("F3 performance   F5 freelook   F6 pause   F7 fog of war");

		if (ImGui::CollapsingHeader("Fog of war: what the thief sees", ImGuiTreeNodeFlags_DefaultOpen))
			DrawFogOfWar(scene);

		if (ImGui::CollapsingHeader("Fog and volumetric light"))
			DrawFog(scene);

		if (ImGui::CollapsingHeader("Weather: lightning and storms"))
			DrawWeather(scene);

		if (ImGui::CollapsingHeader("Guards: sight and hearing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("View cones (cut by walls)", &m_ShowVision);
			ImGui::SameLine();
			ImGui::Checkbox("Labels", &m_ShowLabels);
			ImGui::Checkbox("Noise rings", &m_ShowNoises);
			ImGui::SameLine();
			ImGui::Checkbox("every noise", &m_ShowAllNoises);
			ImGui::Checkbox("Navmesh and paths", &m_ShowNavMesh);
			ImGui::SameLine();
			ImGui::Checkbox("Colliders", &m_ShowColliders);
			if (ImGui::Checkbox("Guards ignore the thief", &m_IgnoreThief))
			{
				if (PerceptionSystem* perception = scene.GetPerceptionSystem())
					perception->SetIgnoreIntruders(m_IgnoreThief);
			}
			ImGui::SetItemTooltip("They neither see, touch nor hear him (his footsteps); bodies, arrows and thrown things still count.");
			if (ImGui::Checkbox("Guards see the fog's glow", &m_FogGlow))
			{
				if (PerceptionSystem* perception = scene.GetPerceptionSystem())
					perception->SetFogGlow(m_FogGlow);
			}
			ImGui::SetItemTooltip("On: the thief against lamp-lit fog stands out and bright fog in between hides him, as the player "
								  "sees it. Off: fog only dims the contrast (the model before it), to compare.");
			if (ImGui::TreeNode("Legend"))
			{
				ImGui::TextWrapped("Cone: the whole field of view, cut where a wall or a closed door stops the eye. Bright: the focus "
								   "cone (full contrast); dim: the side bands, where the thief counts for half. Colour: green unaware, "
								   "yellow suspicious, orange investigating or searching, red alarmed. The bar over the head is "
								   "suspicion. Magenta lines: the thief's body points this guard has in view (light decides whether he "
								   "makes them out: see the label's B, >= 1 is seen).");
				ImGui::TextWrapped("Noise rings: blue, how far it carries at all; orange, where it still reaches 4 (a guard walks "
								   "over). Walls cost 4, closed doors their SoundLoss; sound also goes round along the navmesh.");
				ImGui::TreePop();
			}
		}

		if (ImGui::CollapsingHeader("Test noise", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextUnformatted("Left click in the world:");
			ImGui::SameLine();
			if (ImGui::RadioButton("selects", m_ClickMode == ClickMode::Select))
				m_ClickMode = ClickMode::Select;
			ImGui::SameLine();
			if (ImGui::RadioButton("makes the noise", m_ClickMode == ClickMode::Noise))
				m_ClickMode = ClickMode::Noise;
			ImGui::TextDisabled("Shift+click always makes the noise.");
			ImGui::SliderFloat("Loudness", &m_NoiseLoudness, 0.5f, 10.0f, "%.1f");
			if (ImGui::BeginTable("##references", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
			{
				for (const NoiseReference& reference : NoiseProbe::References())
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					const std::string label = std::format("{:.1f}##{}", reference.Loudness, reference.Example);
					if (ImGui::Selectable(label.c_str(), std::abs(m_NoiseLoudness - reference.Loudness) < 0.05f,
							ImGuiSelectableFlags_SpanAllColumns))
						m_NoiseLoudness = reference.Loudness;
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(reference.Example);
				}
				ImGui::EndTable();
			}
			if (m_LastNoisePosition)
			{
				ImGui::Text("Last test noise %.1f at (%.1f, %.1f, %.1f):", m_LastNoiseLoudness, m_LastNoisePosition->x, m_LastNoisePosition->y,
					m_LastNoisePosition->z);
				if (m_LastHearing.empty())
					ImGui::TextDisabled("  nobody heard it");
				for (const NoiseHearing& hearing : m_LastHearing)
				{
					ImGui::TextColored(ToImVec4(DebugOverlays::StateColor(hearing.State)), "  %s hears %.1f -> %s", hearing.Name.c_str(),
						hearing.Level, std::string(StateName(hearing.State)).c_str());
				}
			}
			if (ImGui::TreeNode("Noises heard in the game"))
			{
				for (const LoggedNoise& noise : m_NoiseLog)
					ImGui::Text("%7.1f s  %4.1f  %s", noise.Time, noise.Loudness, noise.Cause.c_str());
				ImGui::TreePop();
			}
		}

		if (ImGui::CollapsingHeader("Camera and time", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("Freelook (F5)", &m_FreeLook))
				m_FreeLookStarted = false;
			if (m_FreeLook)
			{
				float speed = m_FreeCamera.GetSpeed();
				ImGui::SameLine();
				ImGui::SetNextItemWidth(140.0f * m_AppliedScale);
				if (ImGui::SliderFloat("m/s", &speed, 0.5f, 40.0f, "%.1f", ImGuiSliderFlags_Logarithmic))
					m_FreeCamera.SetSpeed(speed);
				if (ImGui::Button("Back to the thief's camera"))
					m_FreeLookStarted = false;
			}
			bool paused = scene.IsPaused();
			if (ImGui::Checkbox("Paused (F6)", &paused))
				SetPaused(scene, paused);
			ImGui::SameLine();
			ImGui::BeginDisabled(!paused);
			if (ImGui::Button("Step"))
				scene.Step(1);
			ImGui::EndDisabled();
			ImGui::SliderFloat("Time scale", &m_TimeScale, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
			ImGui::SameLine();
			if (ImGui::SmallButton("1x"))
				m_TimeScale = 1.0f;
			ImGui::SliderFloat("UI size", &m_UserScale, 0.5f, 2.0f, "%.2fx");
		}
		ImGui::End();
		Input::SetGameInputBlocked(BlocksGameInput()); // the window's close button
	}

	void RuntimeDebugPanel::DrawFogOfWar(Scene& scene)
	{
		FogOfWarSystem* fog = scene.GetFogOfWarSystem();
		if (ImGui::Checkbox("Fog of war (F7)", &m_FogOfWar) && fog)
			fog->SetForcedOff(!m_FogOfWar);
		ImGui::SetItemTooltip("Off: the whole level is drawn, guards included, as before the fog of war.");

		auto viewers = scene.GetRegistry().view<FogOfWarComponent>();
		if (viewers.empty())
		{
			ImGui::TextDisabled("No FogOfWar component in this level (add one to the player).");
			return;
		}
		// The thief's settings, live (they are not saved: copy good values into the level).
		FogOfWarComponent& component = viewers.get<FogOfWarComponent>(viewers.front());
		ImGui::Checkbox("Enabled on the thief", &component.Enabled);
		ImGui::SameLine();
		ImGui::Checkbox("Hide guards out of sight", &component.HideActors);
		ImGui::Checkbox("The cone is the camera's frame", &component.ConeFromCamera);
		if (component.ConeFromCamera)
			ImGui::BeginDisabled();
		ImGui::SliderFloat("View cone", &component.ViewAngle, 30.0f, 360.0f, "%.0f deg");
		if (component.ConeFromCamera)
			ImGui::EndDisabled();
		ImGui::SliderFloat("Cone edge", &component.EdgeSoftness, 0.0f, 60.0f, "%.0f deg");
		ImGui::SliderFloat("Awareness circle", &component.AwarenessRadius, 0.0f, 6.0f, "%.1f m");
		ImGui::SliderFloat("View distance", &component.ViewDistance, 5.0f, 100.0f, "%.0f m");
		ImGui::SliderFloat("See over low things", &component.SeeOverHeight, 0.0f, 2.0f, "%.2f m");
		ImGui::SliderFloat("Eye follows crouch", &component.EyeSpeed, 0.0f, 30.0f, "%.0f /s");
		ImGui::SliderFloat("Unseen brightness", &component.Brightness, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Unseen colour", &component.Saturation, 0.0f, 1.0f, "%.2f");
		if (fog && fog->GetView())
			ImGui::Text("Guards and bodies hidden now: %u", component.RuntimeHiddenActors);
		else
			ImGui::TextDisabled("Fog is off.");
	}

	void RuntimeDebugPanel::DrawWeather(Scene& scene)
	{
		if (Entity shadowed = scene.GetShadowedDirectionalLight())
			ImGui::Text("Directional shadow map: %s", shadowed.GetName().c_str());
		else
			ImGui::TextDisabled("No directional light casts shadows.");
		ImGui::SetItemTooltip("The brightest directional light with CastShadows gets it; the others casting shadows are left out\n"
							  "meanwhile (a flash takes it from the moon), in the picture and for the guards alike.");

		// The level's weather script (Scripts/World/WorldEvents.lua: anything whose script has strike()).
		ScriptEngine* scripts = scene.GetScriptEngine();
		constexpr const char* Find = "local function weather() for _, e in ipairs(scene.entities_with('Script')) do local s = e:script() "
									 "if s and type(s.strike) == 'function' then return s end end end ";
		std::string output;
		if (!scripts || !scripts->Execute(std::string(Find) + "return weather() ~= nil", output) || output != "true")
		{
			ImGui::TextDisabled("No weather script in this level (WorldEvents).");
			return;
		}
		auto run = [&](const std::string& code) {
			std::string result;
			if (!scripts->Execute(std::string(Find) + "local w = weather() " + code, result))
				PN_CORE_WARN("Weather panel: {}", result);
		};
		if (ImGui::Button("Lightning strike"))
			run("w:strike()");
		ImGui::SetItemTooltip("A flash now (thunder follows, as in a storm).");
		ImGui::SameLine();
		if (ImGui::Button("Start storm"))
			run("w:start_storm()");
		ImGui::SameLine();
		if (ImGui::Button("End storm"))
			run("w:end_storm()");
		if (ImGui::SliderFloat("Flash length", &m_FlashHold, 0.0f, 5.0f, m_FlashHold > 0.0f ? "%.2f s" : "level's own"))
			run(std::format("w.flash_hold = {}", m_FlashHold));
		ImGui::SetItemTooltip("Hold each flash longer to look at what it lights (0: the level's own, 0.15 s).");
	}

	void RuntimeDebugPanel::DrawFog(Scene& scene)
	{
		Entity environmentEntity = scene.GetEnvironmentEntity();
		if (!environmentEntity)
		{
			ImGui::TextDisabled("No Environment in this level.");
			return;
		}
		// The level's fog, live (not saved: copy good values into the level). Guards see through the same fog.
		EnvironmentComponent& environment = environmentEntity.GetComponent<EnvironmentComponent>();
		ImGui::Checkbox("Volumetric (lamps light the fog)", &environment.VolumetricFog);
		ImGui::SliderFloat("Density", &environment.FogDensity, 0.0f, 0.3f, "%.3f /m", ImGuiSliderFlags_Logarithmic);
		ImGui::ColorEdit3("Own glow", &environment.FogColor.x, ImGuiColorEditFlags_Float);
		ImGui::SliderFloat("Height", &environment.FogHeight, -5.0f, 10.0f, "%.1f m");
		ImGui::SetItemTooltip("Full density below this height; above it the fog thins by the falloff.");
		ImGui::SliderFloat("Height falloff", &environment.FogHeightFalloff, 0.0f, 2.0f, "%.2f /m");
		ImGui::SliderFloat("Scattering", &environment.FogScattering, 0.0f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("How much lamp light the fog sends towards the eye.");
		ImGui::SliderFloat("Forward glare", &environment.FogAnisotropy, -0.9f, 0.9f, "%.2f");
		ImGui::SetItemTooltip("0 = the same in every direction; higher = brighter looking into a lamp.");
		const size_t volumes = scene.GetRegistry().view<FogVolumeComponent>().size();
		ImGui::TextDisabled("%zu fog volume(s) in the level (FogVolume; tuned in the editor).", volumes);
	}

	void RuntimeDebugPanel::DrawGuardLabels(Scene& scene, const RenderCamera& camera)
	{
		PerceptionSystem* perception = scene.GetPerceptionSystem();
		if (!perception)
			return;
		const std::optional<Entity> thief = FindThief(scene);
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		for (auto [handle, component] : scene.GetRegistry().view<PerceptionComponent>().each())
		{
			Entity npc(handle, &scene);
			const glm::vec3 anchor = scene.GetWorldPosition(npc) + glm::vec3(0.0f, component.EyeHeight + 0.75f, 0.0f);
			glm::vec2 pixel;
			if (!WorldToScreen(camera, anchor, glm::vec2(display.x, display.y), pixel) || glm::distance(camera.Position, anchor) > 40.0f)
				continue;

			std::string text = npc.GetName();
			if (!PerceptionSystem::IsAwake(npc))
				text += "\n(down)";
			else
			{
				text += std::format("\n{}", StateName(component.RuntimeState));
				if (component.RuntimeReactionTime > 0.0f)
					text += std::format(" {:.1f}, {:.0f} s ({})", component.RuntimeReactionStrength, component.RuntimeReactionTime,
						component.RuntimeStimulusCause);
				text += std::format("\nsuspicion {:.2f}", component.RuntimeSuspicion);
				if (thief)
				{
					const VisibilityResult seen = perception->ComputeVisibility(npc, *thief);
					if (seen.VisiblePoints > 0)
					{
						text += std::format("\nthief: B {:.2f}, {}/{} points, C {:.2f} vs {:.2f}", seen.Stimulus, seen.VisiblePoints, seen.TotalPoints,
							seen.Contrast, seen.ContrastThreshold);
						// As the eye gets them through the fog: the thief, the background, and what the fog adds and lets through.
						text += std::format("\n  L thief {:.4f}, bg {:.4f}, own shading {:.2f}", seen.TargetLuminance, seen.BackgroundLuminance,
							seen.SelfContrast);
						if (seen.FogTransmittance < 0.999f || seen.FogGlowFront > 0.0f || seen.FogGlowBehind > 0.0f)
							text += std::format("\n  fog T {:.2f}, glow front {:.4f}, behind {:.4f}", seen.FogTransmittance, seen.FogGlowFront,
								seen.FogGlowBehind);
					}
					else
						text += "\nthief: not in view";
				}
			}
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			const ImVec2 topLeft(pixel.x - size.x * 0.5f, pixel.y - size.y);
			const float pad = 4.0f * m_AppliedScale;
			draw->AddRectFilled(ImVec2(topLeft.x - pad, topLeft.y - pad), ImVec2(topLeft.x + size.x + pad, topLeft.y + size.y + pad),
				IM_COL32(0, 0, 0, 150), 3.0f * m_AppliedScale);
			draw->AddText(topLeft, ImGui::ColorConvertFloat4ToU32(ToImVec4(DebugOverlays::StateColor(component.RuntimeState))), text.c_str());
		}
	}

	std::optional<glm::vec3> RuntimeDebugPanel::WorldPointUnderMouse(Scene& scene, const RenderCamera& camera)
	{
		PhysicsWorld* physics = scene.GetPhysicsWorld();
		if (!physics)
			return std::nullopt;
		const ImGuiIO& io = ImGui::GetIO();
		const glm::vec3 direction = ScreenRayDirection(camera, glm::vec2(io.MousePos.x, io.MousePos.y), glm::vec2(io.DisplaySize.x, io.DisplaySize.y));
		// Through characters and triggers, onto the floor or a wall.
		auto filter = [&scene](UUID id) {
			std::optional<Entity> entity = scene.TryGetEntityByUUID(id);
			if (!entity || entity->HasComponent<CharacterControllerComponent>())
				return false;
			const RigidBodyComponent* body = entity->TryGetComponent<RigidBodyComponent>();
			return !body || !body->IsTrigger;
		};
		const std::optional<RaycastHit> hit = physics->Raycast(camera.Position, direction, 200.0f, filter);
		if (!hit)
			return std::nullopt;
		return hit->Point + hit->Normal * 0.05f;
	}

	void RuntimeDebugPanel::MakeTestNoise(Scene& scene, const glm::vec3& position)
	{
		m_LastHearing = NoiseProbe::Emit(scene, position, m_NoiseLoudness);
		m_LastNoisePosition = position;
		m_LastNoiseLoudness = m_NoiseLoudness;
		m_Noises.push_back({ position, m_NoiseLoudness, static_cast<float>(ImGui::GetTime()) });
	}

	void RuntimeDebugPanel::HandleWorldClick(Scene& scene, const RenderCamera& camera)
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (!m_PanelOpen)
		{
			m_Placing = false;
			return;
		}
		if (m_Placing && ImGui::IsKeyPressed(ImGuiKey_Escape))
			m_Placing = false;
		if (io.WantCaptureMouse)
			return;

		const glm::vec3 direction = ScreenRayDirection(camera, glm::vec2(io.MousePos.x, io.MousePos.y), glm::vec2(io.DisplaySize.x, io.DisplaySize.y));
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (m_Placing)
			{
				// "Move here": onto the floor or wall under the cursor.
				Entity selected = GetSelection(scene);
				if (const std::optional<glm::vec3> point = WorldPointUnderMouse(scene, camera); point && selected)
					ObjectInspector::MoveTo(scene, selected, *point);
				m_Placing = false;
			}
			else if (m_ClickMode == ClickMode::Noise || io.KeyShift)
			{
				if (const std::optional<glm::vec3> point = WorldPointUnderMouse(scene, camera))
					MakeTestNoise(scene, *point);
			}
			else
			{
				const std::optional<ObjectInspector::PickResult> pick = ObjectInspector::Pick(scene, camera.Position, direction);
				m_Selection = pick ? pick->Object.GetUUID() : UUID(UUID::Invalid);
			}
		}

		// A right click (not a drag: freelook looks with the right button) selects what is under the
		// cursor and opens its menu.
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !m_Placing)
		{
			const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
			if (drag.x * drag.x + drag.y * drag.y < 16.0f)
			{
				if (const std::optional<ObjectInspector::PickResult> pick = ObjectInspector::Pick(scene, camera.Position, direction))
				{
					m_Selection = pick->Object.GetUUID();
					m_OpenObjectMenu = true;
				}
			}
		}
	}

	Entity RuntimeDebugPanel::GetSelection(Scene& scene)
	{
		if (m_Selection == UUID::Invalid)
			return {};
		std::optional<Entity> entity = scene.TryGetEntityByUUID(m_Selection);
		if (!entity)
		{
			m_Selection = UUID::Invalid; // destroyed
			return {};
		}
		return *entity;
	}

	void RuntimeDebugPanel::DrawObjectActions(Scene& scene, Entity entity, bool asMenu)
	{
		auto action = [asMenu](const char* label, bool enabled = true) {
			if (asMenu)
				return ImGui::MenuItem(label, nullptr, false, enabled);
			ImGui::BeginDisabled(!enabled);
			const bool pressed = ImGui::SmallButton(label);
			ImGui::EndDisabled();
			ImGui::SameLine();
			return pressed;
		};
		auto toggle = [asMenu](const char* label, bool& value) {
			if (asMenu)
				return ImGui::MenuItem(label, nullptr, &value);
			const bool changed = ImGui::Checkbox(label, &value);
			ImGui::SameLine();
			return changed;
		};

		PerceptionSystem* perception = scene.GetPerceptionSystem();
		if (entity.HasComponent<PerceptionComponent>() && perception)
		{
			const bool awake = PerceptionSystem::IsAwake(entity);
			if (action("Knock out", awake))
				perception->KnockOut(entity, VitalState::Unconscious);
			if (action("Kill", awake))
				perception->KnockOut(entity, VitalState::Dead);
			if (action("Calm down", awake))
				PerceptionSystem::Calm(entity.GetComponent<PerceptionComponent>());
			bool ignores = perception->IgnoresIntruders(entity);
			if (toggle("Ignores the thief", ignores))
				perception->SetIgnoresIntruders(entity, ignores);
			if (asMenu)
				ImGui::SetItemTooltip("He neither sees, touches nor hears the thief; other noises still count.");
			toggle("Only his view cone", m_OnlySelectedVision);
		}
		if (ObjectInspector::HasLights(entity))
		{
			const bool on = ObjectInspector::AreLightsOn(entity);
			if (action(on ? "Light off" : "Light on"))
				ObjectInspector::SetLightsOn(entity, !on);
		}
		if (action("Move here..."))
			m_Placing = true;
		if (asMenu)
			ImGui::SetItemTooltip("Then left-click where it goes (Esc cancels).");
		if (action("Copy values"))
		{
			ImGui::SetClipboardText(ComponentRegistry::SerializeEntityComponents(entity).dump(2).c_str());
			PN_CORE_INFO("Inspector: the components of '{}' are on the clipboard", entity.GetName());
		}
		if (asMenu)
			ImGui::SetItemTooltip("Its components as JSON (the scene file's form), to carry good values into the level.");
		if (action("Delete"))
		{
			scene.DestroyEntity(entity);
			m_Selection = UUID::Invalid;
		}
		if (!asMenu)
			ImGui::NewLine();
	}

	void RuntimeDebugPanel::DrawObjectMenu(Scene& scene)
	{
		if (m_OpenObjectMenu)
		{
			ImGui::OpenPopup("##object");
			m_OpenObjectMenu = false;
		}
		if (!ImGui::BeginPopup("##object"))
			return;
		if (Entity entity = GetSelection(scene))
		{
			ImGui::TextDisabled("%s  (%s)", entity.GetName().c_str(), ObjectInspector::Describe(entity).Kind.c_str());
			ImGui::Separator();
			DrawObjectActions(scene, entity, true);
		}
		else
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void RuntimeDebugPanel::DrawInspector(Scene& scene)
	{
		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(10.0f * m_AppliedScale, io.DisplaySize.y * 0.25f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(420.0f * m_AppliedScale, io.DisplaySize.y * 0.6f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}
		if (m_Placing)
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Left-click where it goes (Esc cancels).");

		Entity entity = GetSelection(scene);
		if (!entity)
		{
			ImGui::TextWrapped("Click an object in the world to select it: a guard, a lamp, a door, a crate. Right-click for its menu.");
			ImGui::End();
			return;
		}

		const ObjectInspector::ObjectContext context = ObjectInspector::Describe(entity);
		ImGui::Text("%s", entity.GetName().c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("%s", context.Kind.c_str());
		if (Entity parent = entity.GetParent())
		{
			ImGui::TextDisabled("in");
			ImGui::SameLine();
			if (ImGui::SmallButton(parent.GetName().c_str()))
				m_Selection = parent.GetUUID();
		}
		DrawObjectActions(scene, entity, false);
		ImGui::TextDisabled("Changes apply at once and are not saved: Copy values carries them to the level.");
		ImGui::Separator();

		// The components that matter for this kind of thing first, open; then the rest, folded.
		auto drawComponent = [&](const ComponentInfo& info, bool open) {
			ImGui::PushID(info.Name.c_str());
			if (ImGui::CollapsingHeader(info.Name.c_str(), open ? ImGuiTreeNodeFlags_DefaultOpen : 0))
			{
				ComponentInspector::DrawFields(entity, info);
				ComponentInspector::DrawRuntimeState(entity, info);
			}
			ImGui::PopID();
		};
		for (const std::string& name : context.KeyComponents)
		{
			if (const ComponentInfo* info = ComponentRegistry::Find(name))
				drawComponent(*info, true);
		}
		if (ImGui::TreeNodeEx("Other components", ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				if (info.Has(entity) && std::find(context.KeyComponents.begin(), context.KeyComponents.end(), info.Name) == context.KeyComponents.end())
					drawComponent(info, false);
			}
			ImGui::TreePop();
		}
		const std::vector<Entity> children = entity.GetChildren();
		if (!children.empty() && ImGui::TreeNodeEx("Parts", ImGuiTreeNodeFlags_SpanAvailWidth, "Parts (%zu)", children.size()))
		{
			for (Entity child : children)
			{
				const std::string label = std::format("{}  ({})##{}", child.GetName(), ObjectInspector::Describe(child).Kind,
					static_cast<uint64_t>(child.GetUUID()));
				if (ImGui::Selectable(label.c_str()))
					m_Selection = child.GetUUID();
			}
			ImGui::TreePop();
		}
		ImGui::End();
	}

}
