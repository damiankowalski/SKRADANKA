#include "pnpch.h"
#include "Penumbra/Scene/GameState.h"

#include <cctype>
#include <cstdlib>

namespace Penumbra {

	namespace {

		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
				text.remove_prefix(1);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
				text.remove_suffix(1);
			return text;
		}

		ScriptValue ParseValue(std::string_view text)
		{
			if (text == "true")
				return true;
			if (text == "false")
				return false;
			// strtod rather than std::from_chars: Apple's libc++ lacks the floating-point overload. The
			// first character must start a number, so strtod's own leniency (spaces, "inf") stays out.
			const std::string copy(text);
			if (!copy.empty() && (std::isdigit(static_cast<unsigned char>(copy[0])) || copy[0] == '-' || copy[0] == '+' || copy[0] == '.'))
			{
				char* end = nullptr;
				const double number = std::strtod(copy.c_str(), &end);
				if (end == copy.c_str() + copy.size())
					return number;
			}
			return std::string(text);
		}

	}

	bool ParseGameValues(std::string_view text, GameState& out, std::string& error)
	{
		GameState parsed;
		while (!text.empty())
		{
			const size_t comma = text.find(',');
			const std::string_view pair = Trim(text.substr(0, comma));
			text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
			if (pair.empty())
				continue;
			const size_t equals = pair.find('=');
			const std::string_view key = equals == std::string_view::npos ? std::string_view() : Trim(pair.substr(0, equals));
			if (key.empty())
			{
				error = "Expected key=value, got '" + std::string(pair) + "'";
				return false;
			}
			parsed[std::string(key)] = ParseValue(Trim(pair.substr(equals + 1)));
		}
		for (auto& [key, value] : parsed)
			out[key] = std::move(value);
		return true;
	}

}
