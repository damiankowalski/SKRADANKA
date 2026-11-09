#include "pnpch.h"
#include "Penumbra/Core/Input.h"

namespace Penumbra {

	namespace {
		struct InputState
		{
			std::array<bool, Key::MaxKey> KeysDown{};
			std::array<bool, Key::MaxKey> KeysPrevious{};
			std::array<bool, Mouse::MaxButton> ButtonsDown{};
			std::array<bool, Mouse::MaxButton> ButtonsPrevious{};
			glm::vec2 MousePosition{ 0.0f };
			glm::vec2 MousePositionPrevious{ 0.0f };
			bool HasMousePosition = false;
			float Scroll = 0.0f;
		};

		InputState s_State;
	}

	bool Input::IsKeyDown(KeyCode key) { return key < Key::MaxKey && s_State.KeysDown[key]; }
	bool Input::IsKeyPressed(KeyCode key) { return key < Key::MaxKey && s_State.KeysDown[key] && !s_State.KeysPrevious[key]; }
	bool Input::IsKeyReleased(KeyCode key) { return key < Key::MaxKey && !s_State.KeysDown[key] && s_State.KeysPrevious[key]; }
	bool Input::IsMouseButtonDown(MouseCode button) { return button < Mouse::MaxButton && s_State.ButtonsDown[button]; }

	bool Input::IsMouseButtonPressed(MouseCode button)
	{
		return button < Mouse::MaxButton && s_State.ButtonsDown[button] && !s_State.ButtonsPrevious[button];
	}

	glm::vec2 Input::GetMousePosition() { return s_State.MousePosition; }
	glm::vec2 Input::GetMouseDelta() { return s_State.MousePosition - s_State.MousePositionPrevious; }
	float Input::GetScrollDelta() { return s_State.Scroll; }

	void Input::SetKeyState(KeyCode key, bool down)
	{
		if (key < Key::MaxKey)
			s_State.KeysDown[key] = down;
	}

	void Input::SetMouseButtonState(MouseCode button, bool down)
	{
		if (button < Mouse::MaxButton)
			s_State.ButtonsDown[button] = down;
	}

	void Input::SetMousePosition(const glm::vec2& position)
	{
		if (!s_State.HasMousePosition)
		{
			s_State.MousePositionPrevious = position;
			s_State.HasMousePosition = true;
		}
		s_State.MousePosition = position;
	}

	void Input::AddScroll(float delta) { s_State.Scroll += delta; }

	void Input::EndFrame()
	{
		s_State.KeysPrevious = s_State.KeysDown;
		s_State.ButtonsPrevious = s_State.ButtonsDown;
		s_State.MousePositionPrevious = s_State.MousePosition;
		s_State.Scroll = 0.0f;
	}

	void Input::Reset() { s_State = InputState{}; }

}
