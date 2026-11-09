#include "pnpch.h"
#include "Penumbra/Scene/JsonSerialization.h"

#include <charconv>

namespace Penumbra {

	bool ParseUUID(const Json& in, UUID& value, std::string& error)
	{
		if (in.is_number_unsigned())
		{
			value = UUID(in.get<uint64_t>());
			return true;
		}
		if (in.is_string())
		{
			const std::string text = in.get<std::string>();
			uint64_t parsed = 0;
			const auto [ptr, result] = std::from_chars(text.data(), text.data() + text.size(), parsed);
			if (result == std::errc() && ptr == text.data() + text.size())
			{
				value = UUID(parsed);
				return true;
			}
		}
		error = "expected an entity id (decimal string)";
		return false;
	}

	void ToJson(Json& out, const ScriptValue& value)
	{
		std::visit(
			[&](const auto& v) {
				using T = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<T, glm::vec3>)
					ToJson(out, v);
				else
					out = v;
			},
			value);
	}

	bool FromJson(const Json& in, ScriptValue& value, std::string& error)
	{
		if (in.is_boolean())
		{
			value = in.get<bool>();
			return true;
		}
		if (in.is_number())
		{
			value = in.get<double>();
			return true;
		}
		if (in.is_string())
		{
			value = in.get<std::string>();
			return true;
		}
		glm::vec3 vector;
		if (in.is_array() && FromJson(in, vector, error))
		{
			value = vector;
			return true;
		}
		error = "script property must be a boolean, number, string or [x, y, z]";
		return false;
	}

	void ToJson(Json& out, const std::unordered_map<std::string, ScriptValue>& value)
	{
		out = Json::object();
		// Sorted output keeps scene files diff-friendly.
		std::vector<std::string> keys;
		for (const auto& [key, unused] : value)
			keys.push_back(key);
		std::sort(keys.begin(), keys.end());
		for (const std::string& key : keys)
			ToJson(out[key], value.at(key));
	}

	bool FromJson(const Json& in, std::unordered_map<std::string, ScriptValue>& value, std::string& error)
	{
		if (!in.is_object())
		{
			error = "expected an object";
			return false;
		}
		std::unordered_map<std::string, ScriptValue> result;
		for (const auto& [key, element] : in.items())
		{
			ScriptValue parsed;
			std::string elementError;
			if (!FromJson(element, parsed, elementError))
			{
				error = std::format("'{}': {}", key, elementError);
				return false;
			}
			result[key] = std::move(parsed);
		}
		value = std::move(result);
		return true;
	}

}
