#pragma once

#include "Penumbra/Core/UUID.h"
#include "Penumbra/Scene/Components.h"

#include <nlohmann/json.hpp>

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// JSON conversions shared by every serializer. Each FromJson returns false and sets
// error on type mismatch instead of throwing, so bad input from files, editor commands
// or scripts is reported cleanly and never corrupts the scene.

namespace Penumbra {

	using Json = nlohmann::json;

	// --- Enum names -------------------------------------------------------------

	template<typename E>
	struct EnumNames;

	template<>
	struct EnumNames<RigidBodyType>
	{
		static constexpr std::pair<RigidBodyType, std::string_view> Values[] = {
			{ RigidBodyType::Static, "Static" }, { RigidBodyType::Dynamic, "Dynamic" }, { RigidBodyType::Kinematic, "Kinematic" }
		};
	};

	template<>
	struct EnumNames<StimDelivery>
	{
		static constexpr std::pair<StimDelivery, std::string_view> Values[] = {
			{ StimDelivery::Contact, "Contact" }, { StimDelivery::Radius, "Radius" }, { StimDelivery::Continuous, "Continuous" }
		};
	};

	template<>
	struct EnumNames<StimActionType>
	{
		static constexpr std::pair<StimActionType, std::string_view> Values[] = {
			{ StimActionType::DisableLights, "DisableLights" }, { StimActionType::EnableLights, "EnableLights" },
			{ StimActionType::Destroy, "Destroy" }, { StimActionType::SpawnPrefab, "SpawnPrefab" },
			{ StimActionType::EmitStim, "EmitStim" }, { StimActionType::RadialImpulse, "RadialImpulse" },
			{ StimActionType::PlaySound, "PlaySound" }, { StimActionType::EmitNoise, "EmitNoise" },
			{ StimActionType::ScriptEvent, "ScriptEvent" }, { StimActionType::Knockout, "Knockout" },
			{ StimActionType::Damage, "Damage" }, { StimActionType::Alarm, "Alarm" }
		};
	};

	template<>
	struct EnumNames<AwarenessState>
	{
		static constexpr std::pair<AwarenessState, std::string_view> Values[] = {
			{ AwarenessState::Unaware, "Unaware" }, { AwarenessState::Suspicious, "Suspicious" },
			{ AwarenessState::Investigating, "Investigating" }, { AwarenessState::Searching, "Searching" },
			{ AwarenessState::Alarmed, "Alarmed" }
		};
	};

	template<>
	struct EnumNames<TargetKind>
	{
		static constexpr std::pair<TargetKind, std::string_view> Values[] = { { TargetKind::Intruder, "Intruder" }, { TargetKind::Body, "Body" },
			{ TargetKind::Projectile, "Projectile" } };
	};

	template<>
	struct EnumNames<VitalState>
	{
		static constexpr std::pair<VitalState, std::string_view> Values[] = {
			{ VitalState::Awake, "Awake" }, { VitalState::Unconscious, "Unconscious" }, { VitalState::Dead, "Dead" }
		};
	};

	template<>
	struct EnumNames<StimulusKind>
	{
		static constexpr std::pair<StimulusKind, std::string_view> Values[] = {
			{ StimulusKind::None, "None" }, { StimulusKind::Heard, "Heard" }, { StimulusKind::Seen, "Seen" },
			{ StimulusKind::Reported, "Reported" }
		};
	};

	template<>
	struct EnumNames<NavStatus>
	{
		static constexpr std::pair<NavStatus, std::string_view> Values[] = {
			{ NavStatus::Idle, "Idle" }, { NavStatus::Moving, "Moving" }, { NavStatus::Arrived, "Arrived" },
			{ NavStatus::Failed, "Failed" }, { NavStatus::Stuck, "Stuck" }
		};
	};

	template<typename E>
	std::string_view EnumToString(E value)
	{
		for (const auto& [enumValue, name] : EnumNames<E>::Values)
		{
			if (enumValue == value)
				return name;
		}
		return "Unknown";
	}

	template<typename E>
	std::optional<E> EnumFromString(std::string_view text)
	{
		for (const auto& [enumValue, name] : EnumNames<E>::Values)
		{
			if (name == text)
				return enumValue;
		}
		return std::nullopt;
	}

	// --- Scalars ------------------------------------------------------------------

	inline void ToJson(Json& out, float value) { out = value; }
	inline void ToJson(Json& out, bool value) { out = value; }
	inline void ToJson(Json& out, int value) { out = value; }
	inline void ToJson(Json& out, uint32_t value) { out = value; }
	inline void ToJson(Json& out, const std::string& value) { out = value; }
	inline void ToJson(Json& out, UUID value) { out = std::to_string(static_cast<uint64_t>(value)); }

	inline bool FromJson(const Json& in, float& value, std::string& error)
	{
		if (!in.is_number())
		{
			error = "expected a number";
			return false;
		}
		value = in.get<float>();
		return true;
	}

	inline bool FromJson(const Json& in, bool& value, std::string& error)
	{
		if (!in.is_boolean())
		{
			error = "expected a boolean";
			return false;
		}
		value = in.get<bool>();
		return true;
	}

	inline bool FromJson(const Json& in, int& value, std::string& error)
	{
		if (!in.is_number_integer())
		{
			error = "expected an integer";
			return false;
		}
		value = in.get<int>();
		return true;
	}

	inline bool FromJson(const Json& in, uint32_t& value, std::string& error)
	{
		if (!in.is_number_unsigned() && !(in.is_number_integer() && in.get<int64_t>() >= 0))
		{
			error = "expected a non-negative integer";
			return false;
		}
		value = in.get<uint32_t>();
		return true;
	}

	inline bool FromJson(const Json& in, std::string& value, std::string& error)
	{
		if (!in.is_string())
		{
			error = "expected a string";
			return false;
		}
		value = in.get<std::string>();
		return true;
	}

	bool ParseUUID(const Json& in, UUID& value, std::string& error);

	inline bool FromJson(const Json& in, UUID& value, std::string& error) { return ParseUUID(in, value, error); }

	// --- glm ------------------------------------------------------------------------

	template<int N>
	void ToJson(Json& out, const glm::vec<N, float, glm::defaultp>& value)
	{
		out = Json::array();
		for (int i = 0; i < N; i++)
			out.push_back(value[i]);
	}

	template<int N>
	bool FromJson(const Json& in, glm::vec<N, float, glm::defaultp>& value, std::string& error)
	{
		if (!in.is_array() || in.size() != static_cast<size_t>(N))
		{
			error = std::format("expected an array of {} numbers", N);
			return false;
		}
		glm::vec<N, float, glm::defaultp> result;
		for (int i = 0; i < N; i++)
		{
			if (!in[static_cast<size_t>(i)].is_number())
			{
				error = std::format("expected an array of {} numbers", N);
				return false;
			}
			result[i] = in[static_cast<size_t>(i)].get<float>();
		}
		value = result;
		return true;
	}

	// Rotations are stored as Euler angles in degrees: readable and hand-editable.
	inline void ToJson(Json& out, const glm::quat& value) { ToJson(out, Math::EulerDegreesFromQuat(value)); }

	inline bool FromJson(const Json& in, glm::quat& value, std::string& error)
	{
		glm::vec3 degrees;
		if (!FromJson(in, degrees, error))
			return false;
		value = Math::QuatFromEulerDegrees(degrees);
		return true;
	}

	// --- Enums ----------------------------------------------------------------------

	template<typename E>
		requires std::is_enum_v<E>
	void ToJson(Json& out, E value)
	{
		out = std::string(EnumToString(value));
	}

	template<typename E>
		requires std::is_enum_v<E>
	bool FromJson(const Json& in, E& value, std::string& error)
	{
		if (!in.is_string())
		{
			error = "expected an enum name string";
			return false;
		}
		const std::optional<E> parsed = EnumFromString<E>(in.get<std::string>());
		if (!parsed)
		{
			std::string options;
			for (const auto& [enumValue, name] : EnumNames<E>::Values)
				options += std::format("{}{}", options.empty() ? "" : ", ", name);
			error = std::format("unknown value '{}' (expected one of: {})", in.get<std::string>(), options);
			return false;
		}
		value = *parsed;
		return true;
	}

	// --- Script properties ------------------------------------------------------------

	void ToJson(Json& out, const ScriptValue& value);
	bool FromJson(const Json& in, ScriptValue& value, std::string& error);
	void ToJson(Json& out, const std::unordered_map<std::string, ScriptValue>& value);
	bool FromJson(const Json& in, std::unordered_map<std::string, ScriptValue>& value, std::string& error);

	// --- Nested structs -------------------------------------------------------------

	void ToJson(Json& out, const StimAction& value);
	bool FromJson(const Json& in, StimAction& value, std::string& error);
	void ToJson(Json& out, const StimResponseRule& value);
	bool FromJson(const Json& in, StimResponseRule& value, std::string& error);

	// --- Containers -----------------------------------------------------------------

	template<typename T>
	void ToJson(Json& out, const std::vector<T>& values)
	{
		out = Json::array();
		for (const T& value : values)
		{
			Json element;
			ToJson(element, value);
			out.push_back(std::move(element));
		}
	}

	template<typename T>
	bool FromJson(const Json& in, std::vector<T>& values, std::string& error)
	{
		if (!in.is_array())
		{
			error = "expected an array";
			return false;
		}
		std::vector<T> result;
		result.reserve(in.size());
		for (size_t i = 0; i < in.size(); i++)
		{
			T element{};
			std::string elementError;
			if (!FromJson(in[i], element, elementError))
			{
				error = std::format("[{}]: {}", i, elementError);
				return false;
			}
			result.push_back(std::move(element));
		}
		values = std::move(result);
		return true;
	}

	namespace JsonDetail {

		// Reads a struct described by a field visitor from a JSON object (merge semantics,
		// unknown keys rejected).
		template<typename T, typename VisitFn>
		bool ReadObject(const Json& in, T& value, std::string& error, VisitFn&& visit)
		{
			if (!in.is_object())
			{
				error = "expected an object";
				return false;
			}
			T updated = value;
			std::vector<std::string> known;
			bool ok = true;
			visit(updated, [&](const char* name, auto& field) {
				known.emplace_back(name);
				if (!ok)
					return;
				const auto it = in.find(name);
				if (it == in.end())
					return;
				std::string fieldError;
				if (!FromJson(*it, field, fieldError))
				{
					error = std::format("'{}': {}", name, fieldError);
					ok = false;
				}
			});
			if (!ok)
				return false;
			for (const auto& [key, unused] : in.items())
			{
				if (std::find(known.begin(), known.end(), key) == known.end())
				{
					error = std::format("unknown field '{}'", key);
					return false;
				}
			}
			value = std::move(updated);
			return true;
		}

	}

}
