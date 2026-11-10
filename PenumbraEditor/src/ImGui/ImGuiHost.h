#pragma once

#include "Penumbra/Core/Base.h"

#include <nvrhi/nvrhi.h>

struct GLFWwindow;

namespace Penumbra {

	class ImGuiRenderer;

	// Owns the Dear ImGui context, the GLFW platform backend and the NVRHI renderer backend.
	class ImGuiHost
	{
	public:
		ImGuiHost(GLFWwindow* window, nvrhi::IDevice* device);
		~ImGuiHost();

		ImGuiHost(const ImGuiHost&) = delete;
		ImGuiHost& operator=(const ImGuiHost&) = delete;

		void BeginFrame();
		void EndFrame(nvrhi::IFramebuffer* target);

		// True when ImGui wants exclusive use of the keyboard / mouse this frame.
		bool WantsKeyboard() const;
		bool WantsMouse() const;

	private:
		void ApplyStyle();

	private:
		Scope<ImGuiRenderer> m_Renderer;
	};

}
