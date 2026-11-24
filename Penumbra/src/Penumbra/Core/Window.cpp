#include "pnpch.h"
#include "Penumbra/Core/Window.h"

#include "Penumbra/Core/Input.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Penumbra {

	namespace {
		int s_WindowCount = 0;

		void GLFWErrorCallback(int error, const char* description)
		{
			PN_CORE_ERROR("GLFW error {}: {}", error, description);
		}

		Window& FromGLFW(GLFWwindow* window) { return *static_cast<Window*>(glfwGetWindowUserPointer(window)); }
	}

	Window::Window(const WindowSpecification& specification)
		: m_Width(specification.Width), m_Height(specification.Height)
	{
		if (s_WindowCount == 0)
		{
			glfwSetErrorCallback(GLFWErrorCallback);
			PN_CORE_VERIFY(glfwInit() == GLFW_TRUE, "Failed to initialize GLFW");
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, specification.Resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, specification.Maximized ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_VISIBLE, specification.Visible ? GLFW_TRUE : GLFW_FALSE);
		// Full-resolution framebuffers on Retina. Mouse positions stay in window units (points), so code that
		// maps the mouse to the picture uses ratios (position / ImGui's DisplaySize), never framebuffer pixels.
		glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
		// Windows: the requested size is scaled by the monitor's display scaling, like on macOS.
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		m_Window = glfwCreateWindow(static_cast<int>(m_Width), static_cast<int>(m_Height), specification.Title.c_str(), nullptr, nullptr);
		PN_CORE_VERIFY(m_Window != nullptr, "Failed to create window");
		s_WindowCount++;

		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		m_Width = static_cast<uint32_t>(width);
		m_Height = static_cast<uint32_t>(height);

		glfwSetWindowUserPointer(m_Window, this);

		glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int w, int h) {
			Window& self = FromGLFW(window);
			self.m_Width = static_cast<uint32_t>(w);
			self.m_Height = static_cast<uint32_t>(h);
			Event event{ .Type = EventType::WindowResize, .Width = self.m_Width, .Height = self.m_Height };
			self.Dispatch(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
			Event event{ .Type = EventType::WindowClose };
			FromGLFW(window).Dispatch(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int, int action, int) {
			if (key < 0 || key >= Key::MaxKey)
				return;
			const bool down = action != GLFW_RELEASE;
			Input::SetKeyState(static_cast<KeyCode>(key), down);
			Event event{ .Type = down ? EventType::KeyPressed : EventType::KeyReleased, .Key = static_cast<KeyCode>(key), .Repeat = action == GLFW_REPEAT };
			FromGLFW(window).Dispatch(event);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int) {
			if (button < 0 || button >= Mouse::MaxButton)
				return;
			const bool down = action == GLFW_PRESS;
			Input::SetMouseButtonState(static_cast<MouseCode>(button), down);
			Event event{ .Type = down ? EventType::MouseButtonPressed : EventType::MouseButtonReleased, .Button = static_cast<MouseCode>(button) };
			FromGLFW(window).Dispatch(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y) {
			Input::SetMousePosition({ static_cast<float>(x), static_cast<float>(y) });
			Event event{ .Type = EventType::MouseMoved, .X = static_cast<float>(x), .Y = static_cast<float>(y) };
			FromGLFW(window).Dispatch(event);
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double x, double y) {
			Input::AddScroll(static_cast<float>(y));
			Event event{ .Type = EventType::MouseScrolled, .X = static_cast<float>(x), .Y = static_cast<float>(y) };
			FromGLFW(window).Dispatch(event);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths) {
			Event event{ .Type = EventType::FilesDropped };
			for (int i = 0; i < count; i++)
				event.Paths.emplace_back(paths[i]);
			FromGLFW(window).Dispatch(event);
		});
	}

	Window::~Window()
	{
		glfwDestroyWindow(m_Window);
		if (--s_WindowCount == 0)
			glfwTerminate();
	}

	void Window::PollEvents() { glfwPollEvents(); }

	float Window::GetFramebufferScale() const
	{
		int windowWidth = 0, windowHeight = 0;
		glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
		return windowWidth > 0 && m_Width > 0 ? static_cast<float>(m_Width) / static_cast<float>(windowWidth) : 1.0f;
	}

	float Window::GetDpiScale() const
	{
		float contentScale = 1.0f, ignored = 1.0f;
		glfwGetWindowContentScale(m_Window, &contentScale, &ignored);
		return std::max(contentScale / GetFramebufferScale(), 0.5f);
	}

	void Window::SetTitle(const std::string& title) { glfwSetWindowTitle(m_Window, title.c_str()); }

	void Window::SetCursorLocked(bool locked)
	{
		m_CursorLocked = locked;
		glfwSetInputMode(m_Window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		if (locked && glfwRawMouseMotionSupported())
			glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}

	void Window::Dispatch(Event& event)
	{
		if (m_EventCallback)
			m_EventCallback(event);
	}

}
