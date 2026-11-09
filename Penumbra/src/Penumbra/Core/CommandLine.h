#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Penumbra {

	// Parses "--flag", "--key value" and "--key=value" style arguments.
	class CommandLine
	{
	public:
		CommandLine() = default;
		CommandLine(int argc, char** argv);
		explicit CommandLine(std::vector<std::string> arguments);

		bool HasFlag(std::string_view name) const;
		std::optional<std::string> GetValue(std::string_view name) const;
		std::optional<int> GetInt(std::string_view name) const;
		const std::vector<std::string>& GetArguments() const { return m_Arguments; }

	private:
		std::vector<std::string> m_Arguments;
	};

}
