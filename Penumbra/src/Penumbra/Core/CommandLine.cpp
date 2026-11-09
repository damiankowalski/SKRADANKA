#include "pnpch.h"
#include "Penumbra/Core/CommandLine.h"

#include <charconv>

namespace Penumbra {

	namespace {
		// Returns the part after "--" when the argument names this option, either bare
		// ("--name") or with an inline value ("--name=value"); otherwise nullopt.
		std::optional<std::string_view> MatchOption(std::string_view argument, std::string_view name)
		{
			if (!argument.starts_with("--"))
				return std::nullopt;
			const std::string_view body = argument.substr(2);
			if (body == name)
				return body;
			if (body.size() > name.size() && body.substr(0, name.size()) == name && body[name.size()] == '=')
				return body;
			return std::nullopt;
		}
	}

	CommandLine::CommandLine(int argc, char** argv)
	{
		for (int i = 1; i < argc; i++)
			m_Arguments.emplace_back(argv[i]);
	}

	CommandLine::CommandLine(std::vector<std::string> arguments)
		: m_Arguments(std::move(arguments))
	{
	}

	bool CommandLine::HasFlag(std::string_view name) const
	{
		for (const std::string& argument : m_Arguments)
		{
			if (MatchOption(argument, name))
				return true;
		}
		return false;
	}

	std::optional<std::string> CommandLine::GetValue(std::string_view name) const
	{
		for (size_t i = 0; i < m_Arguments.size(); i++)
		{
			const std::optional<std::string_view> body = MatchOption(m_Arguments[i], name);
			if (!body)
				continue;
			if (body->size() > name.size())
				return std::string(body->substr(name.size() + 1));
			if (i + 1 < m_Arguments.size() && !m_Arguments[i + 1].starts_with("--"))
				return m_Arguments[i + 1];
			return std::nullopt;
		}
		return std::nullopt;
	}

	std::optional<int> CommandLine::GetInt(std::string_view name) const
	{
		const std::optional<std::string> value = GetValue(name);
		if (!value)
			return std::nullopt;
		int result = 0;
		const auto [ptr, error] = std::from_chars(value->data(), value->data() + value->size(), result);
		if (error != std::errc() || ptr != value->data() + value->size())
			return std::nullopt;
		return result;
	}

}
