#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/Event.h"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Penumbra {

	struct WindowSpecification
	{
		std::string Title = "Penumbra";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Resizable = true;
		bool Maximized = false;
		bool Visible = true;
	};

	class Window
	{
	public:
		using EventCallback = std::function<void(Event&)>;

		explicit Window(const WindowSpecification& specification);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		void PollEvents();
		void SetEventCallback(EventCallback callback) { m_EventCallback = std::move(callback); }

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		bool IsMinimized() const { return m_Width == 0 || m_Height == 0; }

		void SetTitle(const std::string& title);
		void SetCursorLocked(bool locked);
		bool IsCursorLocked() const { return m_CursorLocked; }

		GLFWwindow* GetNativeWindow() const { return m_Window; }
		void* GetHWND() const;

	private:
		void Dispatch(Event& event);

	private:
		GLFWwindow* m_Window = nullptr;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_CursorLocked = false;
		EventCallback m_EventCallback;
	};

}
