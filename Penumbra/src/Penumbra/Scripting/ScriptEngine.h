#pragma once

#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Scene/Entity.h"

#include <string>
#include <vector>

namespace Penumbra {

	struct ScriptError
	{
		UUID Entity = UUID::Invalid;
		std::string Script;
		std::string Message;
	};

	// Runs the Lua scripts of a playing scene. Each scene runtime gets its own Lua state.
	//
	// A script file returns a table ("class"); every entity with a ScriptComponent gets an
	// instance whose metatable is that class:
	//
	//   local Guard = { Properties = { speed = 2.0 } }
	//   function Guard:on_create() end
	//   function Guard:on_update(dt) self.entity:set_position(...) end
	//   function Guard:on_destroy() end
	//   function Guard:on_collision(other) end
	//   function Guard:on_stim(stim, intensity, source) end
	//   function Guard:on_stim_event(name) end
	//   return Guard
	//
	// Script errors never crash the engine: the failing instance is disabled and the error recorded.
	class ScriptEngine
	{
	public:
		explicit ScriptEngine(Scene& scene);
		~ScriptEngine();

		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;

		void Start();
		void Stop();
		void Update(Timestep ts);

		void OnEntityDestroyed(Entity entity);
		void OnCollision(UUID a, UUID b);
		void OnStim(Entity entity, const std::string& stim, float intensity, UUID source);
		void OnStimEvent(Entity entity, const std::string& name);

		// Executes a chunk of Lua in the scene's state (automation / console). Returns false
		// with the error message in output on failure; on success output holds the result as text.
		bool Execute(const std::string& code, std::string& output);

		bool HasInstance(UUID entity) const;
		const std::vector<ScriptError>& GetErrors() const;
		float GetDeltaTime() const;

	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

}
