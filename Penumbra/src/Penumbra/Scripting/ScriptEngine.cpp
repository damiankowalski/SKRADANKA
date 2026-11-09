#include "pnpch.h"
#include "Penumbra/Scripting/ScriptInternal.h"

#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Project/Project.h"

namespace Penumbra {

	namespace {

		sol::object ScriptValueToLua(sol::state_view lua, const ScriptValue& value)
		{
			return std::visit([&](const auto& v) { return sol::make_object(lua, v); }, value);
		}

		// package.searchers entry: require("Scripts/util") loads <assets>/Scripts/util.lua.
		int AssetSearcher(lua_State* state)
		{
			const char* name = luaL_checkstring(state, 1);
			std::string relative(name);
			std::replace(relative.begin(), relative.end(), '.', '/');
			if (!relative.ends_with(".lua"))
				relative += ".lua";
			const std::filesystem::path path = Project::ResolveAssetPath(relative);
			const std::optional<std::string> source = FileSystem::ReadTextFile(path);
			if (!source)
			{
				lua_pushfstring(state, "\n\tno asset '%s'", relative.c_str());
				return 1;
			}
			const std::string chunkName = "@" + relative;
			if (luaL_loadbuffer(state, source->data(), source->size(), chunkName.c_str()) != LUA_OK)
				return lua_error(state);
			lua_pushstring(state, relative.c_str());
			return 2;
		}

	}

	ScriptEngine::ScriptEngine(Scene& scene)
		: m_Impl(CreateScope<Impl>(scene))
	{
		sol::state& lua = m_Impl->Lua;
		// io/os are intentionally not opened: game scripts have no business touching the file system.
		lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::math, sol::lib::table,
			sol::lib::utf8, sol::lib::debug);

		sol::table searchers = lua["package"]["searchers"];
		searchers.add(&AssetSearcher);
		lua["package"]["path"] = "";
		lua["package"]["cpath"] = "";

		m_Impl->BindAPI();
	}

	ScriptEngine::~ScriptEngine()
	{
		// Instances reference the Lua state; release them before it is closed.
		m_Impl->Instances.clear();
		m_Impl->Classes.clear();
	}

	void ScriptEngine::Impl::ReportError(UUID entity, const std::string& script, const std::string& message)
	{
		std::string name = "<unknown>";
		if (std::optional<Entity> resolved = SceneRef.TryGetEntityByUUID(entity))
			name = resolved->GetName();
		PN_CORE_ERROR("Script error in '{}' (entity '{}'): {}", script, name, message);
		Errors.push_back({ entity, script, message });
	}

	std::optional<sol::table> ScriptEngine::Impl::LoadClass(const std::string& script, std::string& error)
	{
		if (const auto it = Classes.find(script); it != Classes.end())
			return it->second;

		const std::filesystem::path path = Project::ResolveAssetPath(script);
		const std::optional<std::string> source = FileSystem::ReadTextFile(path);
		if (!source)
		{
			error = std::format("cannot read script '{}'", path.string());
			return std::nullopt;
		}

		sol::load_result chunk = Lua.load(*source, "@" + script);
		if (!chunk.valid())
		{
			sol::error loadError = chunk;
			error = loadError.what();
			return std::nullopt;
		}
		sol::protected_function function = chunk;
		sol::protected_function_result result = function();
		if (!result.valid())
		{
			sol::error runError = result;
			error = runError.what();
			return std::nullopt;
		}
		sol::object returned = result.get<sol::object>();
		if (returned.get_type() != sol::type::table)
		{
			error = "script must return a table";
			return std::nullopt;
		}
		sol::table classTable = returned.as<sol::table>();
		classTable["__index"] = classTable;
		Classes.emplace(script, classTable);
		return classTable;
	}

	ScriptInstance* ScriptEngine::Impl::CreateInstance(Entity entity)
	{
		const auto& component = entity.GetComponent<ScriptComponent>();
		ScriptInstance instance;
		instance.Entity = entity.GetUUID();
		instance.Script = component.Script;

		std::string error;
		std::optional<sol::table> classTable = component.Script.empty() ? std::nullopt : LoadClass(component.Script, error);
		if (!classTable)
		{
			ReportError(instance.Entity, component.Script, component.Script.empty() ? "no script assigned" : error);
			instance.Disabled = true;
			instance.Created = true;
		}
		else
		{
			sol::table self = Lua.create_table();
			// Defaults from the class Properties table, then per-entity overrides.
			sol::object defaults = (*classTable)["Properties"];
			if (defaults.get_type() == sol::type::table)
			{
				for (const auto& [key, value] : defaults.as<sol::table>())
					self[key] = value;
			}
			for (const auto& [key, value] : component.Properties)
				self[key] = ScriptValueToLua(Lua, value);
			self["entity"] = Wrap(entity);
			self[sol::metatable_key] = *classTable;
			instance.Self = self;
			sol::table registry = Lua.registry()["penumbra_instances"];
			registry[std::to_string(static_cast<uint64_t>(instance.Entity))] = self;
		}

		Order.push_back(instance.Entity);
		return &Instances.insert_or_assign(instance.Entity, std::move(instance)).first->second;
	}

	void ScriptEngine::Start()
	{
		m_Impl->Running = true;
		Update(Timestep(0.0f));
	}

	void ScriptEngine::Stop()
	{
		m_Impl->Running = false;
		m_Impl->Instances.clear();
		m_Impl->Order.clear();
	}

	void ScriptEngine::Update(Timestep ts)
	{
		Impl& impl = *m_Impl;
		impl.DeltaTime = ts;

		// Instantiate scripts of entities that appeared since the last frame, then run on_create
		// for all of them before anyone's on_update (so spawners see initialized children).
		std::vector<UUID> created;
		for (auto [handle, component] : impl.SceneRef.GetRegistry().view<ScriptComponent>().each())
		{
			Entity entity(handle, &impl.SceneRef);
			if (impl.Instances.contains(entity.GetUUID()) || impl.SceneRef.IsEntityPendingDestroy(entity))
				continue;
			impl.CreateInstance(entity);
			created.push_back(entity.GetUUID());
		}
		for (UUID uuid : created)
		{
			const auto it = impl.Instances.find(uuid);
			if (it == impl.Instances.end() || it->second.Created)
				continue;
			it->second.Created = true;
			impl.Call(it->second, "on_create");
		}

		if (ts <= 0.0f)
			return;

		// Snapshot: scripts may create or destroy entities while we iterate.
		const std::vector<UUID> order = impl.Order;
		for (UUID uuid : order)
		{
			const auto it = impl.Instances.find(uuid);
			if (it == impl.Instances.end())
				continue;
			std::optional<Entity> entity = impl.SceneRef.TryGetEntityByUUID(uuid);
			if (!entity || impl.SceneRef.IsEntityPendingDestroy(*entity))
				continue;
			impl.Call(it->second, "on_update", ts.GetSeconds());
		}
	}

	void ScriptEngine::OnEntityDestroyed(Entity entity)
	{
		const auto it = m_Impl->Instances.find(entity.GetUUID());
		if (it == m_Impl->Instances.end())
			return;
		if (m_Impl->Running && it->second.Created)
			m_Impl->Call(it->second, "on_destroy");
		// The callback may have destroyed other entities, invalidating the iterator.
		m_Impl->Instances.erase(entity.GetUUID());
		sol::table registry = m_Impl->Lua.registry()["penumbra_instances"];
		registry[std::to_string(static_cast<uint64_t>(entity.GetUUID()))] = sol::lua_nil;
		std::erase(m_Impl->Order, entity.GetUUID());
	}

	void ScriptEngine::OnCollision(UUID a, UUID b)
	{
		auto dispatch = [&](UUID self, UUID other) {
			const auto it = m_Impl->Instances.find(self);
			if (it == m_Impl->Instances.end() || !it->second.Created)
				return;
			std::optional<Entity> otherEntity = m_Impl->SceneRef.TryGetEntityByUUID(other);
			if (!otherEntity)
				return;
			m_Impl->Call(it->second, "on_collision", m_Impl->Wrap(*otherEntity));
		};
		dispatch(a, b);
		dispatch(b, a);
	}

	void ScriptEngine::OnStim(Entity entity, const std::string& stim, float intensity, UUID source)
	{
		const auto it = m_Impl->Instances.find(entity.GetUUID());
		if (it == m_Impl->Instances.end() || !it->second.Created)
			return;
		sol::object sourceObject = sol::lua_nil;
		if (std::optional<Entity> sourceEntity = m_Impl->SceneRef.TryGetEntityByUUID(source))
			sourceObject = sol::make_object(m_Impl->Lua, m_Impl->Wrap(*sourceEntity));
		m_Impl->Call(it->second, "on_stim", stim, intensity, sourceObject);
	}

	void ScriptEngine::OnStimEvent(Entity entity, const std::string& name)
	{
		const auto it = m_Impl->Instances.find(entity.GetUUID());
		if (it == m_Impl->Instances.end() || !it->second.Created)
			return;
		m_Impl->Call(it->second, "on_stim_event", name);
	}

	bool ScriptEngine::Execute(const std::string& code, std::string& output)
	{
		sol::protected_function_result result = m_Impl->Lua.safe_script(code, sol::script_pass_on_error, "=automation");
		if (!result.valid())
		{
			sol::error error = result;
			output = error.what();
			return false;
		}
		output.clear();
		if (result.return_count() > 0)
		{
			sol::object value = result.get<sol::object>();
			output = LuaToJson(value).dump();
		}
		return true;
	}

	bool ScriptEngine::HasInstance(UUID entity) const { return m_Impl->Instances.contains(entity); }
	const std::vector<ScriptError>& ScriptEngine::GetErrors() const { return m_Impl->Errors; }
	float ScriptEngine::GetDeltaTime() const { return m_Impl->DeltaTime; }

}
