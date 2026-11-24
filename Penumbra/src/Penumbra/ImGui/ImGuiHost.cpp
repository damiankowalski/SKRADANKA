#include "pnpch.h"
#include "Penumbra/ImGui/ImGuiHost.h"

#include "Penumbra/ImGui/ImGuiFonts.h"
#include "Penumbra/ImGui/ImGuiRenderer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

namespace Penumbra {

	ImGuiHost::ImGuiHost(GLFWwindow* window, nvrhi::IDevice* device, const std::filesystem::path& layoutFile)
		: m_LayoutFile(layoutFile.string())
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
		if (!layoutFile.empty() && layoutFile.has_parent_path())
		{
			std::error_code error;
			std::filesystem::create_directories(layoutFile.parent_path(), error);
			if (error)
				PN_CORE_WARN("Panel layouts will not be saved: cannot create {} ({})", layoutFile.parent_path().string(), error.message());
		}
		io.IniFilename = m_LayoutFile.empty() ? nullptr : m_LayoutFile.c_str();
		ImGuiFonts::Load(io);
		ApplyStyle();
		m_BaseStyle = ImGui::GetStyle();
		ImGui_ImplGlfw_InitForOther(window, true);
		m_Renderer = CreateScope<ImGuiRenderer>(device);
	}

	ImGuiHost::~ImGuiHost()
	{
		m_Renderer->Shutdown();
		m_Renderer.reset();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiHost::BeginFrame()
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiHost::EndFrame(nvrhi::IFramebuffer* target)
	{
		ImGui::Render();
		m_Renderer->Render(ImGui::GetDrawData(), target);
	}

	bool ImGuiHost::WantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }
	bool ImGuiHost::WantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }

	void ImGuiHost::SetScale(float scale)
	{
		if (scale == m_Scale)
			return;
		m_Scale = scale;
		ImGuiStyle& style = ImGui::GetStyle();
		style = m_BaseStyle;
		style.ScaleAllSizes(scale);
		style.FontScaleMain = scale;
	}

	void ImGuiHost::ApplyStyle()
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.FontSizeBase = ImGuiFonts::BaseSize;
		style.WindowRounding = 2.0f;
		style.FrameRounding = 2.0f;
		style.TabRounding = 2.0f;
		style.WindowBorderSize = 1.0f;
		style.FramePadding = ImVec2(6.0f, 3.0f);

		// Muted, low-light palette with a cold accent - fitting for a stealth engine.
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.12f, 0.98f);
		colors[ImGuiCol_Header] = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.28f, 0.34f, 1.0f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.34f, 0.42f, 1.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.0f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.26f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.27f, 0.32f, 1.0f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
		colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.30f, 0.38f, 0.50f, 1.0f);
		colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.24f, 0.30f, 1.0f);
		colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.34f, 0.44f, 1.0f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.42f, 0.54f, 1.0f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.62f, 0.90f, 1.0f);
	}

}
