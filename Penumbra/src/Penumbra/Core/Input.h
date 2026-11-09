#pragma once

#include "Penumbra/Core/KeyCodes.h"

#include <glm/vec2.hpp>

namespace Penumbra {

	// Input state is fed by the window (or injected by tests / the automation layer)
	// and sampled by gameplay code. Edge queries ("pressed this frame") are valid until EndFrame().
	class Input
	{
	public:
		static bool IsKeyDown(KeyCode key);
		static bool IsKeyPressed(KeyCode key);
		static bool IsKeyReleased(KeyCode key);
		static bool IsMouseButtonDown(MouseCode button);
		static bool IsMouseButtonPressed(MouseCode button);
		static glm::vec2 GetMousePosition();
		static glm::vec2 GetMouseDelta();
		static float GetScrollDelta();

		static void SetKeyState(KeyCode key, bool down);
		static void SetMouseButtonState(MouseCode button, bool down);
		static void SetMousePosition(const glm::vec2& position);
		static void AddScroll(float delta);

		// Called once per frame after gameplay has consumed input.
		static void EndFrame();
		static void Reset();
	};

}
