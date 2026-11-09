#pragma once

// Internal to the scripting module: shared between ScriptEngine.cpp and ScriptBindings.cpp.

#include "Penumbra/Scripting/ScriptEngine.h"

#include <nlohmann/json.hpp>
#include <sol/sol.hpp>

#include <stdexcept>

namespace Penumbra {

	// Handle given to Lua for an entity. Stores the UUID (not the EnTT handle), so a script
	// holding a reference to a destroyed entity gets a clean error instead of touching a
	// recycled slot.
	struct ScriptEntity
	{
		UUID ID = UUID::Invalid;
		Scene* SceneRef = nullptr;

		std::optional<Entity> TryResolve() const
		{
			if (!SceneRef || !ID.IsValid())
				return std::nullopt;
			return SceneRef->TryGetEntityByUUID(ID);
		}

		Entity Resolve() const
		{
			std::optional<Entity> entity = TryResolve();
			if (!entity)
				throw std::runtime_error("entity has been destroyed");
			return *entity;
		}

		bool operator==(const ScriptEntity& other) const { return ID == other.ID && SceneRef == other.SceneRef; }
	};

	struct ScriptInstance
	{
		UUID Entity = UUID::Invalid;
		std::string Script;
		sol::table Self;
		bool Created = false;
		bool Disabled = false;
	};

	struct ScriptEngine::Impl
	{
		Scene& SceneRef;
		sol::state Lua;
		std::unordered_map<std::string, sol::table> Classes; // script path -> class table
		std::unordered_map<UUID, ScriptInstance> Instances;
		std::vector<UUID> Order; // creation order, for deterministic update order
		std::vector<ScriptError> Errors;
		float DeltaTime = 0.0f;
		bool Running = false;

		explicit Impl(Scene& scene) : SceneRef(scene) {}

		void BindAPI();
		std::optional<sol::table> LoadClass(const std::string& script, std::string& error);
		ScriptInstance* CreateInstance(Entity entity);
		void Call(ScriptInstance& instance, const char* function, auto&&... args);
		void ReportError(UUID entity, const std::string& script, const std::string& message);
		ScriptEntity Wrap(Entity entity) const { return ScriptEntity{ entity.GetUUID(), &SceneRef }; }
	};

	// JSON <-> Lua conversion used by the generic component accessors.
	sol::object JsonToLua(sol::state_view lua, const nlohmann::json& value);
	nlohmann::json LuaToJson(const sol::object& value);

	void ScriptEngine::Impl::Call(ScriptInstance& instance, const char* function, auto&&... args)
	{
		if (instance.Disabled)
			return;
		sol::object member = instance.Self[function];
		if (member.get_type() != sol::type::function)
			return;
		sol::protected_function callable = member.as<sol::protected_function>();
		sol::protected_function_result result = callable(instance.Self, std::forward<decltype(args)>(args)...);
		if (!result.valid())
		{
			sol::error error = result;
			ReportError(instance.Entity, instance.Script, std::format("{}: {}", function, error.what()));
			instance.Disabled = true;
		}
	}

}
