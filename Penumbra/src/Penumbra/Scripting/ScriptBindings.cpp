#include "pnpch.h"
#include "Penumbra/Scripting/ScriptInternal.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Animation/AnimationSystem.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Audio/AudioScene.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/JsonSerialization.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Stim/StimSystem.h"
#include "Penumbra/UI/Overlay.h"

// The complete Lua API. Every global table bound here is documented in docs/ScriptingAPI.md
// and exercised by DemoProject/Assets/Scripts/Tests/FeatureTest.lua.

namespace Penumbra {

	using json = nlohmann::json;

	sol::object JsonToLua(sol::state_view lua, const json& value)
	{
		switch (value.type())
		{
			case json::value_t::boolean:         return sol::make_object(lua, value.get<bool>());
			case json::value_t::number_integer:  return sol::make_object(lua, value.get<int64_t>());
			case json::value_t::number_unsigned: return sol::make_object(lua, static_cast<double>(value.get<uint64_t>()));
			case json::value_t::number_float:    return sol::make_object(lua, value.get<double>());
			case json::value_t::string:          return sol::make_object(lua, value.get<std::string>());
			case json::value_t::array:
			{
				// [x, y, z] number arrays become vec3 so math works directly on component fields.
				if (value.size() == 3 && value[0].is_number() && value[1].is_number() && value[2].is_number())
					return sol::make_object(lua, glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
				sol::table table = lua.create_table(static_cast<int>(value.size()), 0);
				for (size_t i = 0; i < value.size(); i++)
					table[i + 1] = JsonToLua(lua, value[i]);
				return table;
			}
			case json::value_t::object:
			{
				sol::table table = lua.create_table(0, static_cast<int>(value.size()));
				for (const auto& [key, element] : value.items())
					table[key] = JsonToLua(lua, element);
				return table;
			}
			default:
				return sol::lua_nil;
		}
	}

	json LuaToJson(const sol::object& value)
	{
		switch (value.get_type())
		{
			case sol::type::boolean: return value.as<bool>();
			case sol::type::number:
			{
				const double number = value.as<double>();
				if (std::floor(number) == number && std::abs(number) < 9007199254740992.0)
					return static_cast<int64_t>(number);
				return number;
			}
			case sol::type::string: return value.as<std::string>();
			case sol::type::userdata:
			{
				if (value.is<glm::vec3>())
				{
					const glm::vec3 v = value.as<glm::vec3>();
					return json::array({ v.x, v.y, v.z });
				}
				if (value.is<glm::vec4>())
				{
					const glm::vec4 v = value.as<glm::vec4>();
					return json::array({ v.x, v.y, v.z, v.w });
				}
				if (value.is<ScriptEntity>())
					return std::to_string(static_cast<uint64_t>(value.as<ScriptEntity>().ID));
				throw std::runtime_error("cannot convert userdata to data");
			}
			case sol::type::table:
			{
				sol::table table = value.as<sol::table>();
				const size_t length = table.size();
				size_t count = 0;
				for (const auto& entry : table)
				{
					(void)entry;
					count++;
				}
				if (count == 0)
					return json::array();
				if (length == count)
				{
					json array = json::array();
					for (size_t i = 1; i <= length; i++)
						array.push_back(LuaToJson(table[i]));
					return array;
				}
				json object = json::object();
				for (const auto& [key, element] : table)
				{
					if (key.get_type() != sol::type::string)
						throw std::runtime_error("tables must have either only string keys or be sequences");
					object[key.as<std::string>()] = LuaToJson(element);
				}
				return object;
			}
			case sol::type::lua_nil:
			case sol::type::none: return nullptr;
			default: throw std::runtime_error("unsupported Lua value type");
		}
	}

	namespace {

		const ComponentInfo& RequireComponent(const std::string& name)
		{
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!info)
				throw std::runtime_error(std::format("unknown component '{}'", name));
			return *info;
		}

		KeyCode RequireKey(const std::string& name)
		{
			std::optional<KeyCode> key = KeyCodeFromName(name);
			if (!key)
				throw std::runtime_error(std::format("unknown key '{}'", name));
			return *key;
		}

		MouseCode RequireButton(const std::string& name)
		{
			std::optional<MouseCode> button = MouseCodeFromName(name);
			if (!button)
				throw std::runtime_error(std::format("unknown mouse button '{}' (Left, Right, Middle)", name));
			return *button;
		}

		std::string JoinArgs(sol::variadic_args args, sol::this_state state)
		{
			sol::state_view lua(state);
			sol::protected_function toString = lua["tostring"];
			std::string text;
			for (auto argument : args)
			{
				if (!text.empty())
					text += ' ';
				sol::protected_function_result converted = toString(argument);
				text += converted.valid() ? converted.get<std::string>() : std::string("<?>");
			}
			return text;
		}

		glm::vec4 ColorFrom(sol::optional<sol::table> color)
		{
			if (!color)
				return glm::vec4(1.0f);
			sol::table table = *color;
			return glm::vec4(table.get_or(1, 1.0f), table.get_or(2, 1.0f), table.get_or(3, 1.0f), table.get_or(4, 1.0f));
		}

		sol::table HitToTable(sol::state_view lua, Scene& scene, const RaycastHit& hit)
		{
			sol::table table = lua.create_table();
			if (std::optional<Entity> entity = scene.TryGetEntityByUUID(hit.Entity))
				table["entity"] = ScriptEntity{ hit.Entity, &scene };
			table["point"] = hit.Point;
			table["normal"] = hit.Normal;
			table["distance"] = hit.Distance;
			return table;
		}

	}

	void ScriptEngine::Impl::BindAPI()
	{
		sol::state& lua = Lua;
		Scene* scene = &SceneRef;

		// --- vec3 -------------------------------------------------------------------------
		lua.new_usertype<glm::vec3>("vec3",
			sol::call_constructor, sol::factories(
				[]() { return glm::vec3(0.0f); },
				[](float s) { return glm::vec3(s); },
				[](float x, float y, float z) { return glm::vec3(x, y, z); }),
			"x", &glm::vec3::x,
			"y", &glm::vec3::y,
			"z", &glm::vec3::z,
			"length", [](const glm::vec3& v) { return glm::length(v); },
			"normalized", [](const glm::vec3& v) { return glm::length(v) > Math::Epsilon ? glm::normalize(v) : glm::vec3(0.0f); },
			"dot", [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
			"cross", [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); },
			"distance", [](const glm::vec3& a, const glm::vec3& b) { return glm::distance(a, b); },
			"lerp", [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); },
			sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
			sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
			sol::meta_function::multiplication, sol::overload(
				[](const glm::vec3& a, float s) { return a * s; },
				[](float s, const glm::vec3& a) { return a * s; },
				[](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
			sol::meta_function::division, [](const glm::vec3& a, float s) { return a / s; },
			sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
			sol::meta_function::equal_to, [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
			sol::meta_function::to_string, [](const glm::vec3& v) { return std::format("vec3({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z); });

		// --- Entity -----------------------------------------------------------------------
		lua.new_usertype<ScriptEntity>("Entity",
			sol::no_constructor,
			"id", sol::readonly_property([](const ScriptEntity& e) { return std::to_string(static_cast<uint64_t>(e.ID)); }),
			"valid", [](const ScriptEntity& e) { return e.TryResolve().has_value() && !e.SceneRef->IsEntityPendingDestroy(*e.TryResolve()); },
			"get_name", [](const ScriptEntity& e) { return e.Resolve().GetName(); },
			"set_name", [](const ScriptEntity& e, const std::string& name) { e.Resolve().GetComponent<TagComponent>().Tag = name; },
			"get_position", [](const ScriptEntity& e) { return e.SceneRef->GetWorldPosition(e.Resolve()); },
			"set_position", [](const ScriptEntity& e, const glm::vec3& position) {
				Entity entity = e.Resolve();
				glm::vec3 translation, scale;
				glm::quat rotation;
				Math::DecomposeTransform(e.SceneRef->GetWorldTransform(entity), translation, rotation, scale);
				e.SceneRef->SetWorldTransform(entity, Math::ComposeTransform(position, rotation, scale));
				if (PhysicsWorld* physics = e.SceneRef->GetPhysicsWorld())
					physics->Teleport(entity, position, rotation);
			},
			"get_local_position", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Translation; },
			"set_local_position", [](const ScriptEntity& e, const glm::vec3& p) { e.Resolve().GetComponent<TransformComponent>().Translation = p; },
			"get_rotation", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().GetEulerDegrees(); },
			"set_rotation", [](const ScriptEntity& e, const glm::vec3& degrees) {
				Entity entity = e.Resolve();
				entity.GetComponent<TransformComponent>().SetEulerDegrees(degrees);
				if (PhysicsWorld* physics = e.SceneRef->GetPhysicsWorld(); physics && entity.HasComponent<RigidBodyComponent>())
					physics->Teleport(entity, e.SceneRef->GetWorldPosition(entity), e.SceneRef->GetWorldRotation(entity));
			},
			"get_scale", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Scale; },
			"set_scale", [](const ScriptEntity& e, const glm::vec3& s) { e.Resolve().GetComponent<TransformComponent>().Scale = s; },
			"forward", [](const ScriptEntity& e) { return Math::Forward(e.SceneRef->GetWorldRotation(e.Resolve())); },
			"right", [](const ScriptEntity& e) { return Math::Right(e.SceneRef->GetWorldRotation(e.Resolve())); },
			"up", [](const ScriptEntity& e) { return Math::Up(e.SceneRef->GetWorldRotation(e.Resolve())); },
			"look_at", [](const ScriptEntity& e, const glm::vec3& target, sol::optional<bool> yawOnly) {
				Entity entity = e.Resolve();
				glm::vec3 direction = target - e.SceneRef->GetWorldPosition(entity);
				if (yawOnly.value_or(false))
					direction.y = 0.0f;
				if (glm::length(direction) < Math::Epsilon)
					return;
				glm::quat world = Math::LookRotation(direction);
				if (Entity parent = entity.GetParent())
					world = glm::inverse(e.SceneRef->GetWorldRotation(parent)) * world;
				entity.GetComponent<TransformComponent>().Rotation = world;
			},
			"has", [](const ScriptEntity& e, const std::string& component) { return RequireComponent(component).Has(e.Resolve()); },
			"get", [](const ScriptEntity& e, const std::string& component, sol::this_state state) -> sol::object {
				Entity entity = e.Resolve();
				const ComponentInfo& info = RequireComponent(component);
				if (!info.Has(entity))
					return sol::lua_nil;
				return JsonToLua(state, info.Serialize(entity, ComponentSerializeOptions{ .IncludeRuntimeState = true }));
			},
			"set", [](const ScriptEntity& e, const std::string& component, const sol::table& data) {
				Entity entity = e.Resolve();
				const ComponentInfo& info = RequireComponent(component);
				if (!info.Has(entity))
					throw std::runtime_error(std::format("entity has no '{}' component (use add)", component));
				json value = LuaToJson(data);
				if (value.is_array() && value.empty())
					value = json::object();
				std::string error;
				if (!info.Deserialize(entity, value, error))
					throw std::runtime_error(std::format("{}: {}", component, error));
			},
			"add", [](const ScriptEntity& e, const std::string& component, sol::optional<sol::table> data) {
				Entity entity = e.Resolve();
				const ComponentInfo& info = RequireComponent(component);
				info.Add(entity);
				if (data)
				{
					json value = LuaToJson(*data);
					if (value.is_array() && value.empty())
						value = json::object();
					std::string error;
					if (!info.Deserialize(entity, value, error))
						throw std::runtime_error(std::format("{}: {}", component, error));
				}
			},
			"remove", [](const ScriptEntity& e, const std::string& component) {
				const ComponentInfo& info = RequireComponent(component);
				if (!info.Removable)
					throw std::runtime_error(std::format("component '{}' cannot be removed", component));
				Entity entity = e.Resolve();
				if (info.Has(entity))
					info.Remove(entity);
			},
			"parent", [](const ScriptEntity& e) -> sol::optional<ScriptEntity> {
				Entity parent = e.Resolve().GetParent();
				if (!parent)
					return sol::nullopt;
				return ScriptEntity{ parent.GetUUID(), e.SceneRef };
			},
			"children", [](const ScriptEntity& e, sol::this_state state) {
				sol::state_view view(state);
				sol::table result = view.create_table();
				int index = 1;
				for (Entity child : e.Resolve().GetChildren())
					result[index++] = ScriptEntity{ child.GetUUID(), e.SceneRef };
				return result;
			},
			"set_parent", [](const ScriptEntity& e, sol::optional<ScriptEntity> parent) {
				Entity entity = e.Resolve();
				if (parent)
					e.SceneRef->SetParent(entity, parent->Resolve(), true);
				else
					e.SceneRef->Unparent(entity, true);
			},
			"destroy", [](const ScriptEntity& e) {
				if (std::optional<Entity> entity = e.TryResolve())
					e.SceneRef->DestroyEntity(*entity);
			},
			"script", [](const ScriptEntity& e, sol::this_state state) -> sol::object {
				// Access another entity's script instance (for cross-script communication).
				sol::state_view view(state);
				sol::object instances = view.registry()["penumbra_instances"];
				if (instances.get_type() != sol::type::table)
					return sol::lua_nil;
				return instances.as<sol::table>()[std::to_string(static_cast<uint64_t>(e.ID))];
			},
			sol::meta_function::equal_to, [](const ScriptEntity& a, const ScriptEntity& b) { return a == b; },
			sol::meta_function::to_string, [](const ScriptEntity& e) {
				std::optional<Entity> entity = e.TryResolve();
				return std::format("Entity({})", entity ? entity->GetName() : std::string("<destroyed>"));
			});

		// --- log --------------------------------------------------------------------------
		sol::table log = lua.create_named_table("log");
		log.set_function("trace", [](sol::variadic_args args, sol::this_state s) { Log::Write(LogLevel::Trace, "Lua", JoinArgs(args, s)); });
		log.set_function("info", [](sol::variadic_args args, sol::this_state s) { Log::Write(LogLevel::Info, "Lua", JoinArgs(args, s)); });
		log.set_function("warn", [](sol::variadic_args args, sol::this_state s) { Log::Write(LogLevel::Warn, "Lua", JoinArgs(args, s)); });
		log.set_function("error", [](sol::variadic_args args, sol::this_state s) { Log::Write(LogLevel::Error, "Lua", JoinArgs(args, s)); });
		lua.set_function("print", [](sol::variadic_args args, sol::this_state s) { Log::Write(LogLevel::Info, "Lua", JoinArgs(args, s)); });

		// --- time -------------------------------------------------------------------------
		sol::table time = lua.create_named_table("time");
		time.set_function("delta", [this]() { return DeltaTime; });
		time.set_function("elapsed", [scene]() { return scene->GetTime(); });
		time.set_function("frame", [scene]() { return static_cast<double>(scene->GetFrameCount()); });

		// --- scene ------------------------------------------------------------------------
		sol::table sceneTable = lua.create_named_table("scene");
		sceneTable.set_function("create", [scene](sol::optional<std::string> name) {
			return ScriptEntity{ scene->CreateEntity(name.value_or("Entity")).GetUUID(), scene };
		});
		sceneTable.set_function("find", [scene](const std::string& name) -> sol::optional<ScriptEntity> {
			Entity entity = scene->FindEntityByName(name);
			if (!entity || scene->IsEntityPendingDestroy(entity))
				return sol::nullopt;
			return ScriptEntity{ entity.GetUUID(), scene };
		});
		sceneTable.set_function("get", [scene](const std::string& id) -> sol::optional<ScriptEntity> {
			UUID uuid;
			std::string error;
			if (!ParseUUID(json(id), uuid, error) || !scene->TryGetEntityByUUID(uuid))
				return sol::nullopt;
			return ScriptEntity{ uuid, scene };
		});
		sceneTable.set_function("entities_with", [scene](const std::string& component, sol::this_state state) {
			const ComponentInfo& info = RequireComponent(component);
			sol::state_view view(state);
			sol::table result = view.create_table();
			int index = 1;
			for (Entity entity : scene->GetAllEntities())
			{
				if (info.Has(entity) && !scene->IsEntityPendingDestroy(entity))
					result[index++] = ScriptEntity{ entity.GetUUID(), scene };
			}
			return result;
		});
		sceneTable.set_function("instantiate",
			[scene](const std::string& prefab, sol::optional<glm::vec3> position, sol::optional<glm::vec3> rotation) {
				std::string error;
				Entity root = Prefab::Instantiate(*scene, prefab, position.value_or(glm::vec3(0.0f)),
					Math::QuatFromEulerDegrees(rotation.value_or(glm::vec3(0.0f))), &error);
				if (!root)
					throw std::runtime_error(error);
				return ScriptEntity{ root.GetUUID(), scene };
			});
		sceneTable.set_function("destroy", [scene](const ScriptEntity& e) {
			if (std::optional<Entity> entity = e.TryResolve())
				scene->DestroyEntity(*entity);
		});
		sceneTable.set_function("load", [scene](const std::string& path) { scene->RequestSceneLoad(path); });
		sceneTable.set_function("quit", [scene](sol::optional<int> code) { scene->RequestQuit(code.value_or(0)); });
		sceneTable.set_function("count", [scene]() { return static_cast<double>(scene->GetEntityCount()); });

		// --- input ------------------------------------------------------------------------
		sol::table input = lua.create_named_table("input");
		input.set_function("key", [](const std::string& key) { return Input::IsKeyDown(RequireKey(key)); });
		input.set_function("key_pressed", [](const std::string& key) { return Input::IsKeyPressed(RequireKey(key)); });
		input.set_function("key_released", [](const std::string& key) { return Input::IsKeyReleased(RequireKey(key)); });
		input.set_function("mouse_button", [](const std::string& button) { return Input::IsMouseButtonDown(RequireButton(button)); });
		input.set_function("mouse_pressed", [](const std::string& button) { return Input::IsMouseButtonPressed(RequireButton(button)); });
		input.set_function("mouse_position", []() {
			const glm::vec2 position = Input::GetMousePosition();
			return std::make_tuple(position.x, position.y);
		});
		input.set_function("mouse_delta", []() {
			const glm::vec2 delta = Input::GetMouseDelta();
			return std::make_tuple(delta.x, delta.y);
		});
		input.set_function("scroll", []() { return Input::GetScrollDelta(); });
		input.set_function("set_cursor_locked", [scene](bool locked) { scene->RequestCursorLock(locked); });

		// --- physics ----------------------------------------------------------------------
		auto requirePhysics = [scene]() -> PhysicsWorld& {
			PhysicsWorld* physics = scene->GetPhysicsWorld();
			if (!physics)
				throw std::runtime_error("physics is only available while the scene is running");
			return *physics;
		};
		sol::table physics = lua.create_named_table("physics");
		physics.set_function("raycast", [scene, requirePhysics](const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
											sol::optional<ScriptEntity> ignore, sol::this_state state) -> sol::object {
			const UUID ignored = ignore ? ignore->ID : UUID(UUID::Invalid);
			PhysicsEntityFilter filter = [ignored](UUID entity) { return entity != ignored; };
			std::optional<RaycastHit> hit = requirePhysics().Raycast(origin, direction, maxDistance, filter);
			if (!hit)
				return sol::lua_nil;
			return HitToTable(state, *scene, *hit);
		});
		physics.set_function("overlap_sphere", [scene, requirePhysics](const glm::vec3& center, float radius, sol::this_state state) {
			sol::state_view view(state);
			sol::table result = view.create_table();
			int index = 1;
			for (UUID uuid : requirePhysics().OverlapSphere(center, radius))
				result[index++] = ScriptEntity{ uuid, scene };
			return result;
		});
		physics.set_function("add_impulse", [requirePhysics](const ScriptEntity& e, const glm::vec3& v) { requirePhysics().AddImpulse(e.Resolve(), v); });
		physics.set_function("add_force", [requirePhysics](const ScriptEntity& e, const glm::vec3& v) { requirePhysics().AddForce(e.Resolve(), v); });
		physics.set_function("set_velocity", [requirePhysics](const ScriptEntity& e, const glm::vec3& v) { requirePhysics().SetLinearVelocity(e.Resolve(), v); });
		physics.set_function("get_velocity", [requirePhysics](const ScriptEntity& e) { return requirePhysics().GetLinearVelocity(e.Resolve()); });
		physics.set_function("radial_impulse", [requirePhysics](const glm::vec3& c, float r, float s) { requirePhysics().AddRadialImpulse(c, r, s); });
		physics.set_function("get_gravity", [requirePhysics]() { return requirePhysics().GetGravity(); });
		physics.set_function("set_gravity", [requirePhysics](const glm::vec3& g) { requirePhysics().SetGravity(g); });

		// --- character --------------------------------------------------------------------
		auto requireController = [](const ScriptEntity& e) -> CharacterControllerComponent& {
			Entity entity = e.Resolve();
			auto* controller = entity.TryGetComponent<CharacterControllerComponent>();
			if (!controller)
				throw std::runtime_error(std::format("'{}' has no CharacterController", entity.GetName()));
			return *controller;
		};
		sol::table character = lua.create_named_table("character");
		character.set_function("move", [requireController, scene](const ScriptEntity& e, const glm::vec3& velocity) {
			auto& controller = requireController(e);
			// Steering by hand takes the character back from navigation.
			if (NavigationSystem* navigation = scene->GetNavigationSystem())
			{
				const NavAgentComponent* agent = e.Resolve().TryGetComponent<NavAgentComponent>();
				if (agent && agent->RuntimeStatus == NavStatus::Moving)
					navigation->Stop(e.Resolve());
			}
			controller.RuntimeDesiredVelocity = velocity;
		});
		character.set_function("jump", [requireController](const ScriptEntity& e, float speed) {
			auto& controller = requireController(e);
			controller.RuntimeJumpRequested = true;
			controller.RuntimeJumpSpeed = speed;
		});
		character.set_function("is_grounded", [requireController](const ScriptEntity& e) { return requireController(e).RuntimeGrounded; });
		character.set_function("get_velocity", [requireController](const ScriptEntity& e) { return requireController(e).RuntimeVelocity; });

		// --- audio ------------------------------------------------------------------------
		auto requireAudio = [scene]() -> AudioScene& {
			AudioScene* audio = scene->GetAudioScene();
			if (!audio)
				throw std::runtime_error("audio is only available while the scene is running");
			return *audio;
		};
		sol::table audio = lua.create_named_table("audio");
		audio.set_function("play", [requireAudio](const ScriptEntity& e) { return requireAudio().Play(e.Resolve()); });
		audio.set_function("stop", [requireAudio](const ScriptEntity& e) { requireAudio().StopSource(e.Resolve()); });
		audio.set_function("is_playing", [requireAudio](const ScriptEntity& e) { return requireAudio().IsPlaying(e.Resolve()); });
		audio.set_function("play_at", [requireAudio](const std::string& clip, const glm::vec3& position, sol::optional<float> volume,
										  sol::optional<float> noiseLoudness) {
			return requireAudio().PlayOneShot(clip, position, volume.value_or(1.0f), noiseLoudness.value_or(0.0f));
		});

		// --- ai ---------------------------------------------------------------------------
		auto requirePerception = [scene]() -> PerceptionSystem& {
			PerceptionSystem* perception = scene->GetPerceptionSystem();
			if (!perception)
				throw std::runtime_error("AI is only available while the scene is running");
			return *perception;
		};
		auto requirePerceptionComponent = [](const ScriptEntity& e) -> PerceptionComponent& {
			Entity entity = e.Resolve();
			auto* perception = entity.TryGetComponent<PerceptionComponent>();
			if (!perception)
				throw std::runtime_error(std::format("'{}' has no Perception component", entity.GetName()));
			return *perception;
		};
		sol::table ai = lua.create_named_table("ai");
		ai.set_function("emit_noise", [requirePerception](const glm::vec3& position, float loudness, sol::optional<ScriptEntity> source,
										  sol::optional<std::string> cause) {
			requirePerception().EmitNoise(position, loudness, source ? source->ID : UUID(UUID::Invalid), cause.value_or("Noise"));
		});
		ai.set_function("noise_range", [](float loudness) { return PerceptionModel::NoiseRange(loudness); });
		ai.set_function("hearing", [requirePerception, requirePerceptionComponent](const ScriptEntity& observer, const glm::vec3& position,
									   float loudness) {
			requirePerceptionComponent(observer);
			return requirePerception().HearingLevel(observer.Resolve(), position, loudness);
		});
		ai.set_function("notify", [requirePerception, requirePerceptionComponent](const ScriptEntity& observer, const glm::vec3& position,
									  float strength, sol::optional<std::string> cause) {
			requirePerceptionComponent(observer);
			return requirePerception().Notify(observer.Resolve(), position, strength, StimulusKind::Reported, cause.value_or("Reported"));
		});
		ai.set_function("explain_away", [requirePerception, scene](float strength, const std::string& cause, sol::optional<sol::table> options,
										   sol::this_state state) {
			std::optional<glm::vec3> center;
			float radius = 0.0f, spread = 0.0f;
			if (options)
			{
				if (sol::optional<glm::vec3> c = (*options)["center"])
					center = *c;
				radius = (*options).get_or("radius", 0.0f);
				spread = (*options).get_or("spread", 0.0f);
				if (center && radius <= 0.0f)
					throw std::runtime_error("ai.explain_away: 'center' needs a positive 'radius'");
			}
			sol::state_view view(state);
			sol::table calmed = view.create_table();
			int index = 1;
			for (Entity entity : requirePerception().ExplainAway(strength, cause, center, radius, spread))
				calmed[index++] = ScriptEntity{ entity.GetUUID(), scene };
			return calmed;
		});
		ai.set_function("calm", [requirePerceptionComponent](const ScriptEntity& e) { PerceptionSystem::Calm(requirePerceptionComponent(e)); });
		ai.set_function("alertness", [requirePerceptionComponent](const ScriptEntity& e) { return requirePerceptionComponent(e).RuntimeAlertness; });
		ai.set_function("reaction", [requirePerceptionComponent](const ScriptEntity& e, sol::this_state state) {
			const auto& perception = requirePerceptionComponent(e);
			sol::state_view view(state);
			sol::table table = view.create_table();
			table["state"] = std::string(EnumToString(perception.RuntimeState));
			table["strength"] = perception.RuntimeReactionStrength;
			table["time_left"] = perception.RuntimeReactionTime;
			table["kind"] = std::string(EnumToString(perception.RuntimeStimulusKind));
			table["cause"] = perception.RuntimeStimulusCause;
			if (perception.RuntimeHasStimulus)
				table["position"] = perception.RuntimeLastStimulusPosition;
			return table;
		});
		ai.set_function("luminance", [requirePerception](const glm::vec3& position) { return requirePerception().SampleLuminance(position); });
		ai.set_function("state", [requirePerceptionComponent](const ScriptEntity& e) {
			return std::string(EnumToString(requirePerceptionComponent(e).RuntimeState));
		});
		ai.set_function("suspicion", [requirePerceptionComponent](const ScriptEntity& e) { return requirePerceptionComponent(e).RuntimeSuspicion; });
		ai.set_function("set_suspicion", [requirePerceptionComponent](const ScriptEntity& e, float value) {
			requirePerceptionComponent(e).RuntimeSuspicion = std::clamp(value, 0.0f, 1.0f);
		});
		ai.set_function("last_stimulus", [requirePerceptionComponent](const ScriptEntity& e) -> sol::optional<glm::vec3> {
			const auto& perception = requirePerceptionComponent(e);
			if (!perception.RuntimeHasStimulus)
				return sol::nullopt;
			return perception.RuntimeLastStimulusPosition;
		});
		ai.set_function("visibility", [requirePerception](const ScriptEntity& observer, const ScriptEntity& target, sol::this_state state) {
			const VisibilityResult result = requirePerception().ComputeVisibility(observer.Resolve(), target.Resolve());
			sol::state_view view(state);
			sol::table table = view.create_table();
			table["stimulus"] = result.Stimulus;
			table["visible_fraction"] = result.VisibleFraction;
			table["contrast"] = result.Contrast;
			table["threshold"] = result.ContrastThreshold;
			table["size"] = result.SizeFactor;
			table["peripheral"] = result.Peripheral;
			table["motion"] = result.Motion;
			table["atmosphere"] = result.Atmosphere;
			table["target_luminance"] = result.TargetLuminance;
			table["background_luminance"] = result.BackgroundLuminance;
			table["distance"] = result.Distance;
			table["visible_points"] = result.VisiblePoints;
			table["total_points"] = result.TotalPoints;
			return table;
		});

		// --- nav (navmesh pathfinding) ----------------------------------------------------
		auto requireNavigation = [scene]() -> NavigationSystem& {
			NavigationSystem* navigation = scene->GetNavigationSystem();
			if (!navigation)
				throw std::runtime_error("navigation is only available while the scene is running");
			return *navigation;
		};
		sol::table nav = lua.create_named_table("nav");
		nav.set_function("move_to", [requireNavigation](const ScriptEntity& e, const glm::vec3& destination, sol::optional<float> speed) {
			Entity entity = e.Resolve();
			if (!entity.HasComponent<CharacterControllerComponent>())
				throw std::runtime_error(std::format("'{}' has no CharacterController", entity.GetName()));
			return requireNavigation().MoveTo(entity, destination, speed ? std::optional<float>(*speed) : std::nullopt);
		});
		nav.set_function("stop", [requireNavigation](const ScriptEntity& e) { requireNavigation().Stop(e.Resolve()); });
		nav.set_function("status", [](const ScriptEntity& e) {
			const NavAgentComponent* agent = e.Resolve().TryGetComponent<NavAgentComponent>();
			return std::string(EnumToString(agent ? agent->RuntimeStatus : NavStatus::Idle));
		});
		nav.set_function("remaining", [](const ScriptEntity& e) {
			const NavAgentComponent* agent = e.Resolve().TryGetComponent<NavAgentComponent>();
			return agent && agent->RuntimeStatus == NavStatus::Moving ? agent->RuntimeRemainingDistance : 0.0f;
		});
		nav.set_function("path", [requireNavigation](const glm::vec3& from, const glm::vec3& to, sol::this_state state) -> sol::object {
			std::optional<NavPath> path = requireNavigation().FindPath(from, to);
			if (!path)
				return sol::lua_nil;
			sol::state_view view(state);
			sol::table points = view.create_table();
			for (size_t i = 0; i < path->Points.size(); ++i)
				points[i + 1] = path->Points[i];
			sol::table table = view.create_table();
			table["points"] = points;
			table["length"] = path->Length;
			table["complete"] = path->Complete;
			return table;
		});
		nav.set_function("distance", [requireNavigation](const glm::vec3& from, const glm::vec3& to) -> sol::optional<float> {
			std::optional<NavPath> path = requireNavigation().FindPath(from, to);
			if (!path || !path->Complete)
				return sol::nullopt;
			return path->Length;
		});
		nav.set_function("closest_point", [requireNavigation](const glm::vec3& point, sol::optional<float> radius) -> sol::optional<glm::vec3> {
			std::optional<glm::vec3> result = requireNavigation().ClosestPoint(point, radius.value_or(1.5f));
			return result ? sol::optional<glm::vec3>(*result) : sol::nullopt;
		});
		nav.set_function("random_point", [requireNavigation](const glm::vec3& center, float radius) -> sol::optional<glm::vec3> {
			std::optional<glm::vec3> result = requireNavigation().RandomPoint(center, radius);
			return result ? sol::optional<glm::vec3>(*result) : sol::nullopt;
		});
		nav.set_function("rebuild", [requireNavigation]() {
			std::string error;
			const bool built = requireNavigation().Bake(error);
			if (!built)
				Log::Write(LogLevel::Warn, "Lua", error);
			return built;
		});
		nav.set_function("polygon_count", [requireNavigation]() {
			const NavMesh* navMesh = requireNavigation().GetNavMesh();
			return navMesh ? static_cast<double>(navMesh->GetPolygonCount()) : 0.0;
		});

		// --- stim -------------------------------------------------------------------------
		auto requireStim = [scene]() -> StimSystem& {
			StimSystem* stim = scene->GetStimSystem();
			if (!stim)
				throw std::runtime_error("stims are only available while the scene is running");
			return *stim;
		};
		sol::table stim = lua.create_named_table("stim");
		stim.set_function("emit", [requireStim](const std::string& name, const glm::vec3& position, float radius, float intensity,
									  sol::optional<ScriptEntity> source) {
			requireStim().Emit(name, position, radius, intensity, source ? source->ID : UUID(UUID::Invalid));
		});
		stim.set_function("send", [requireStim](const ScriptEntity& target, const std::string& name, float intensity, sol::optional<ScriptEntity> source) {
			requireStim().Deliver(target.Resolve(), name, intensity, source ? source->ID : UUID(UUID::Invalid));
		});
		stim.set_function("set_lights", [](const ScriptEntity& e, bool enabled) { StimSystem::SetLightsEnabled(e.Resolve(), enabled); });

		// --- anim (skeletal animation) --------------------------------------------------------
		auto requireAnimator = [](const ScriptEntity& e) -> std::pair<AnimatorComponent&, std::shared_ptr<const MeshData>> {
			Entity entity = e.Resolve();
			auto* animator = entity.TryGetComponent<AnimatorComponent>();
			if (!animator)
				throw std::runtime_error(std::format("'{}' has no Animator", entity.GetName()));
			const MeshComponent* mesh = entity.TryGetComponent<MeshComponent>();
			return { *animator, mesh ? AssetManager::GetMesh(mesh->Mesh) : nullptr };
		};
		sol::table anim = lua.create_named_table("anim");
		anim.set_function("play", [requireAnimator](const ScriptEntity& e, const std::string& clip, sol::optional<float> fade, sol::optional<bool> loop) {
			auto [animator, mesh] = requireAnimator(e);
			std::string error;
			const std::optional<bool> loopValue = loop ? std::optional<bool>(*loop) : std::nullopt;
			if (!AnimationSystem::Play(animator, mesh.get(), clip, fade.value_or(-1.0f), loopValue, error))
				throw std::runtime_error(error);
		});
		anim.set_function("set_speed", [requireAnimator](const ScriptEntity& e, float speed) { requireAnimator(e).first.Speed = speed; });
		anim.set_function("is_playing", [requireAnimator](const ScriptEntity& e, sol::optional<std::string> clip) {
			const AnimatorComponent& animator = requireAnimator(e).first;
			return AnimationSystem::IsPlaying(animator, clip ? &*clip : nullptr);
		});
		anim.set_function("clips", [requireAnimator](const ScriptEntity& e, sol::this_state state) {
			sol::state_view view(state);
			sol::table result = view.create_table();
			if (const std::shared_ptr<const MeshData> mesh = requireAnimator(e).second)
			{
				for (size_t i = 0; i < mesh->Animations.size(); i++)
					result[i + 1] = mesh->Animations[i].Name;
			}
			return result;
		});

		// --- ui (HUD overlay) -------------------------------------------------------------
		sol::table ui = lua.create_named_table("ui");
		ui.set_function("text", [scene](float x, float y, const std::string& text, sol::optional<float> scale, sol::optional<sol::table> color) {
			scene->GetOverlay().AddText({ { x, y }, text, scale.value_or(2.0f), ColorFrom(color) });
		});
		ui.set_function("rect", [scene](float x, float y, float w, float h, sol::optional<sol::table> color) {
			scene->GetOverlay().AddRect({ { x, y }, { w, h }, ColorFrom(color) });
		});
		ui.set_function("width", [scene]() { return static_cast<double>(scene->GetViewportWidth()); });
		ui.set_function("height", [scene]() { return static_cast<double>(scene->GetViewportHeight()); });

		// --- test -------------------------------------------------------------------------
		sol::table test = lua.create_named_table("test");
		auto record = [scene](bool passed, const std::string& message) {
			TestReport& report = scene->GetTestReport();
			report.Assertions++;
			if (!passed)
			{
				report.Failures++;
				report.Messages.push_back(message);
				PN_CORE_ERROR("[test] FAILED: {}", message);
			}
		};
		test.set_function("check", [record](bool condition, sol::optional<std::string> message) {
			record(condition, message.value_or("assertion"));
			return condition;
		});
		test.set_function("equal", [record](const sol::object& a, const sol::object& b, sol::optional<std::string> message) {
			const json left = LuaToJson(a), right = LuaToJson(b);
			const bool passed = left == right;
			record(passed, std::format("{}: expected {} == {}", message.value_or("equal"), left.dump(), right.dump()));
			return passed;
		});
		test.set_function("near", [record](double a, double b, sol::optional<double> epsilon, sol::optional<std::string> message) {
			const bool passed = std::abs(a - b) <= epsilon.value_or(1e-4);
			record(passed, std::format("{}: expected {} ~= {}", message.value_or("near"), a, b));
			return passed;
		});
		test.set_function("complete", [scene]() {
			TestReport& report = scene->GetTestReport();
			report.Completed = true;
			report.Passed = report.Failures == 0;
			PN_CORE_INFO("[test] completed: {} assertions, {} failures", report.Assertions, report.Failures);
		});
		test.set_function("failures", [scene]() { return scene->GetTestReport().Failures; });

		// Registry table mapping entity id -> script instance (for Entity:script()).
		lua.registry()["penumbra_instances"] = lua.create_table();
	}

}
