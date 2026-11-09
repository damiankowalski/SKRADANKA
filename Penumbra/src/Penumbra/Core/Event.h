#pragma once

#include "Penumbra/Core/KeyCodes.h"

#include <string>
#include <vector>

namespace Penumbra {

	enum class EventType : uint8_t
	{
		None = 0,
		WindowResize,
		WindowClose,
		KeyPressed,
		KeyReleased,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled,
		FilesDropped
	};

	// A single flat event type keeps dispatch trivial; only the fields relevant
	// to the event's Type are meaningful.
	struct Event
	{
		EventType Type = EventType::None;
		bool Handled = false;

		uint32_t Width = 0;
		uint32_t Height = 0;
		KeyCode Key = 0;
		bool Repeat = false;
		MouseCode Button = 0;
		float X = 0.0f;
		float Y = 0.0f;
		std::vector<std::string> Paths;
	};

}
