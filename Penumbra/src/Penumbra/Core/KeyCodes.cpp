#include "pnpch.h"
#include "Penumbra/Core/KeyCodes.h"

namespace Penumbra {

	namespace {
		struct KeyName
		{
			std::string_view Name;
			KeyCode Code;
		};

		constexpr KeyName s_NamedKeys[] = {
			{ "Space", Key::Space }, { "Escape", Key::Escape }, { "Enter", Key::Enter }, { "Tab", Key::Tab },
			{ "Backspace", Key::Backspace }, { "Delete", Key::Delete }, { "Insert", Key::Insert },
			{ "Right", Key::Right }, { "Left", Key::Left }, { "Down", Key::Down }, { "Up", Key::Up },
			{ "PageUp", Key::PageUp }, { "PageDown", Key::PageDown }, { "Home", Key::Home }, { "End", Key::End },
			{ "LeftShift", Key::LeftShift }, { "LeftControl", Key::LeftControl }, { "LeftAlt", Key::LeftAlt },
			{ "RightShift", Key::RightShift }, { "RightControl", Key::RightControl }, { "RightAlt", Key::RightAlt },
			{ "Minus", Key::Minus }, { "Equal", Key::Equal }, { "Comma", Key::Comma }, { "Period", Key::Period },
			{ "Slash", Key::Slash }, { "Semicolon", Key::Semicolon }, { "GraveAccent", Key::GraveAccent },
		};
	}

	std::optional<KeyCode> KeyCodeFromName(std::string_view name)
	{
		if (name.size() == 1)
		{
			const char c = name[0];
			if (c >= 'A' && c <= 'Z')
				return static_cast<KeyCode>(Key::A + (c - 'A'));
			if (c >= 'a' && c <= 'z')
				return static_cast<KeyCode>(Key::A + (c - 'a'));
			if (c >= '0' && c <= '9')
				return static_cast<KeyCode>(Key::D0 + (c - '0'));
		}
		if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F')
		{
			int number = 0;
			for (size_t i = 1; i < name.size(); i++)
			{
				if (name[i] < '0' || name[i] > '9')
					return std::nullopt;
				number = number * 10 + (name[i] - '0');
			}
			if (number >= 1 && number <= 12)
				return static_cast<KeyCode>(Key::F1 + number - 1);
			return std::nullopt;
		}
		for (const KeyName& key : s_NamedKeys)
		{
			if (key.Name == name)
				return key.Code;
		}
		return std::nullopt;
	}

	std::string_view KeyCodeToName(KeyCode key)
	{
		static const std::string_view s_Letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		static const std::string_view s_Digits = "0123456789";
		static const std::string_view s_Function[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
		if (key >= Key::A && key <= Key::Z)
			return s_Letters.substr(key - Key::A, 1);
		if (key >= Key::D0 && key <= Key::D9)
			return s_Digits.substr(key - Key::D0, 1);
		if (key >= Key::F1 && key <= Key::F12)
			return s_Function[key - Key::F1];
		for (const KeyName& named : s_NamedKeys)
		{
			if (named.Code == key)
				return named.Name;
		}
		return "Unknown";
	}

	std::optional<MouseCode> MouseCodeFromName(std::string_view name)
	{
		if (name == "Left")
			return Mouse::ButtonLeft;
		if (name == "Right")
			return Mouse::ButtonRight;
		if (name == "Middle")
			return Mouse::ButtonMiddle;
		return std::nullopt;
	}

}
