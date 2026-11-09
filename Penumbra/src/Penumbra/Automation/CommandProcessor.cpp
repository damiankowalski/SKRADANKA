#include "pnpch.h"
#include "Penumbra/Automation/CommandProcessor.h"

#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Project/ProjectExporter.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/JsonSerialization.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

namespace Penumbra {

	using json = nlohmann::json;

	namespace {

		constexpr size_t MaxLogMessages = 512;

		struct CommandError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		const json& Require(const json& args, const char* key)
		{
			const auto it = args.find(key);
			if (it == args.end())
				throw CommandError(std::format("missing argument '{}'", key));
			return *it;
		}

		std::string RequireString(const json& args, const char* key)
		{
			const json& value = Require(args, key);
			if (!value.is_string())
				throw CommandError(std::format("argument '{}' must be a string", key));
			return value.get<std::string>();
		}

		template<typename T>
		T Optional(const json& args, const char* key, T fallback)
		{
			const auto it = args.find(key);
			if (it == args.end() || it->is_null())
				return fallback;
			try
			{
				return it->get<T>();
			}
			catch (const json::exception&)
			{
				throw CommandError(std::format("argument '{}' has the wrong type", key));
			}
		}

		glm::vec3 OptionalVec3(const json& args, const char* key, const glm::vec3& fallback)
		{
			const auto it = args.find(key);
			if (it == args.end())
				return fallback;
			glm::vec3 value;
			std::string error;
			if (!FromJson(*it, value, error))
				throw CommandError(std::format("argument '{}': {}", key, error));
			return value;
		}

		// Entities are addressed by "id" (UUID string) or by unique "name".
		Entity RequireEntity(Scene& scene, const json& args, const char* idKey = "id", const char* nameKey = "name")
		{
			if (const auto it = args.find(idKey); it != args.end())
			{
				UUID uuid;
				std::string error;
				if (!ParseUUID(*it, uuid, error))
					throw CommandError(std::format("argument '{}': {}", idKey, error));
				std::optional<Entity> entity = scene.TryGetEntityByUUID(uuid);
				if (!entity)
					throw CommandError(std::format("no entity with id {}", static_cast<uint64_t>(uuid)));
				return *entity;
			}
			if (const auto it = args.find(nameKey); it != args.end() && it->is_string())
			{
				Entity entity = scene.FindEntityByName(it->get<std::string>());
				if (!entity)
					throw CommandError(std::format("no entity named '{}'", it->get<std::string>()));
				return entity;
			}
			throw CommandError(std::format("missing argument '{}' (or '{}')", idKey, nameKey));
		}

		std::string IdString(Entity entity) { return std::to_string(static_cast<uint64_t>(entity.GetUUID())); }

		json DescribeEntity(Entity entity, bool runtime)
		{
			json result = json::object();
			result["id"] = IdString(entity);
			result["name"] = entity.GetName();
			if (Entity parent = entity.GetParent())
				result["parent"] = IdString(parent);
			json children = json::array();
			for (Entity child : entity.GetChildren())
				children.push_back(IdString(child));
			result["children"] = children;
			result["components"] = ComponentRegistry::SerializeEntityComponents(entity, { .IncludeRuntimeState = runtime });
			result["worldPosition"] = json::array();
			const glm::vec3 position = entity.GetScene()->GetWorldPosition(entity);
			result["worldPosition"] = { position.x, position.y, position.z };
			return result;
		}

		Ref<Scene> RequireScene(AutomationHost& host)
		{
			Ref<Scene> scene = host.GetActiveScene();
			if (!scene)
				throw CommandError("no scene is open");
			return scene;
		}

		void RequireProject()
		{
			if (!Project::IsLoaded())
				throw CommandError("no project is loaded (use project.open or project.new)");
		}

		// Asset paths given to commands must stay inside the project's asset directory.
		std::filesystem::path ResolveInsideAssets(const std::string& relative)
		{
			RequireProject();
			const std::filesystem::path base = std::filesystem::weakly_canonical(Project::GetAssetDirectory());
			const std::filesystem::path resolved = std::filesystem::weakly_canonical(base / relative);
			const auto [baseEnd, unused] = std::mismatch(base.begin(), base.end(), resolved.begin(), resolved.end());
			if (baseEnd != base.end())
				throw CommandError(std::format("path '{}' is outside the asset directory", relative));
			return resolved;
		}

		void ApplyComponents(Entity entity, const json& components)
		{
			std::string error;
			if (!ComponentRegistry::DeserializeEntityComponents(entity, components, error))
				throw CommandError(error);
		}

	}

	CommandProcessor::CommandProcessor(AutomationHost& host)
		: m_Host(host)
	{
		m_LogSink = Log::AddSink([this](const LogMessage& message) {
			std::scoped_lock lock(m_LogMutex);
			m_RecentLog.push_back(message);
			while (m_RecentLog.size() > MaxLogMessages)
				m_RecentLog.pop_front();
		});
		RegisterAll();
	}

	CommandProcessor::~CommandProcessor() { Log::RemoveSink(m_LogSink); }

	void CommandProcessor::Register(const std::string& name, const std::string& arguments, const std::string& description,
		std::function<json(const json&)> handler)
	{
		m_Commands[name] = { arguments, description, std::move(handler) };
	}

	json CommandProcessor::Execute(const json& request)
	{
		if (!request.is_object())
			return { { "ok", false }, { "error", "request must be a JSON object" } };
		const auto commandIt = request.find("cmd");
		if (commandIt == request.end() || !commandIt->is_string())
			return { { "ok", false }, { "error", "request needs a string 'cmd' field" } };

		const std::string name = commandIt->get<std::string>();
		const auto it = m_Commands.find(name);
		if (it == m_Commands.end())
			return { { "ok", false }, { "cmd", name }, { "error", std::format("unknown command '{}' (try 'help')", name) } };

		try
		{
			json result = it->second.Handler(request);
			return { { "ok", true }, { "cmd", name }, { "result", std::move(result) } };
		}
		catch (const std::exception& exception)
		{
			return { { "ok", false }, { "cmd", name }, { "error", exception.what() } };
		}
	}

	json CommandProcessor::ExecuteBatch(const std::string& text, bool continueOnError)
	{
		std::vector<json> requests;
		const json document = json::parse(text, nullptr, false);
		if (!document.is_discarded() && document.is_array())
		{
			for (const json& request : document)
				requests.push_back(request);
		}
		else if (!document.is_discarded() && document.is_object())
		{
			requests.push_back(document);
		}
		else
		{
			// JSON Lines: one request per line, '#' comments allowed.
			std::istringstream stream(text);
			std::string line;
			while (std::getline(stream, line))
			{
				const size_t first = line.find_first_not_of(" \t\r");
				if (first == std::string::npos || line[first] == '#')
					continue;
				json request = json::parse(line, nullptr, false);
				if (request.is_discarded())
					return json::array({ { { "ok", false }, { "error", std::format("invalid JSON line: {}", line) } } });
				requests.push_back(std::move(request));
			}
		}

		json responses = json::array();
		for (const json& request : requests)
		{
			json response = Execute(request);
			const bool ok = response.value("ok", false);
			responses.push_back(std::move(response));
			if (!ok && !continueOnError)
				break;
		}
		return responses;
	}

	void CommandProcessor::RegisterAll()
	{
		AutomationHost& host = m_Host;

		// ----------------------------------------------------------------- meta
		Register("help", "", "List all commands with their arguments.", [this](const json&) {
			json commands = json::array();
			for (const auto& [name, info] : m_Commands)
				commands.push_back({ { "cmd", name }, { "args", info.Arguments }, { "description", info.Description } });
			return commands;
		});

		Register("log.tail", "count?", "Most recent engine log messages.", [this](const json& args) {
			const size_t count = Optional<size_t>(args, "count", 50);
			std::scoped_lock lock(m_LogMutex);
			json messages = json::array();
			const size_t start = m_RecentLog.size() > count ? m_RecentLog.size() - count : 0;
			for (size_t i = start; i < m_RecentLog.size(); i++)
				messages.push_back({ { "level", LogLevelToString(m_RecentLog[i].Level) }, { "logger", m_RecentLog[i].Logger }, { "text", m_RecentLog[i].Text } });
			return messages;
		});

		// ----------------------------------------------------------------- project
		Register("project.new", "path, name", "Create a project folder (with Assets/Scenes, Scripts, Prefabs) and open it.", [&host](const json& args) {
			ProjectConfig config;
			config.Name = RequireString(args, "name");
			std::string error;
			host.StopPlay();
			if (!Project::Create(RequireString(args, "path"), config, error))
				throw CommandError(error);
			host.SetEditScene(CreateRef<Scene>());
			return json{ { "directory", Project::GetProjectDirectory().string() } };
		});

		Register("project.open", "path", "Open a project (directory or project.json) and its start scene.", [&host](const json& args) {
			std::string error;
			host.StopPlay();
			if (!Project::Load(RequireString(args, "path"), error))
				throw CommandError(error);
			AssetManager::Clear();
			Prefab::ClearCache();
			Ref<Scene> scene = CreateRef<Scene>();
			const std::filesystem::path start = Project::ResolveAssetPath(Project::GetConfig().StartScene);
			json result = { { "directory", Project::GetProjectDirectory().string() }, { "scene", nullptr } };
			if (std::filesystem::exists(start))
			{
				if (!SceneSerializer::Deserialize(*scene, start, error))
					throw CommandError(error);
				result["scene"] = Project::GetConfig().StartScene;
			}
			host.SetEditScene(scene);
			return result;
		});

		Register("project.info", "", "Project settings and directories.", [](const json&) {
			RequireProject();
			const ProjectConfig& config = Project::GetConfig();
			return json{ { "name", config.Name }, { "startScene", config.StartScene }, { "assetDirectory", config.AssetDirectory },
				{ "windowWidth", config.WindowWidth }, { "windowHeight", config.WindowHeight },
				{ "directory", Project::GetProjectDirectory().string() } };
		});

		Register("project.set", "name?, startScene?, windowWidth?, windowHeight?", "Change project settings and save project.json.", [](const json& args) {
			RequireProject();
			ProjectConfig& config = Project::GetConfig();
			config.Name = Optional<std::string>(args, "name", config.Name);
			config.StartScene = Optional<std::string>(args, "startScene", config.StartScene);
			config.WindowWidth = std::max(Optional<uint32_t>(args, "windowWidth", config.WindowWidth), 1u);
			config.WindowHeight = std::max(Optional<uint32_t>(args, "windowHeight", config.WindowHeight), 1u);
			if (!Project::SaveActive())
				throw CommandError("cannot write project.json");
			return json::object();
		});

		Register("project.export", "output", "Export a standalone game (runtime executable + assets) to a folder.", [&host](const json& args) {
			RequireProject();
			const ExportResult result =
				ProjectExporter::Export(Project::GetProjectDirectory(), RequireString(args, "output"), host.GetRuntimeExecutable());
			if (!result.Success)
				throw CommandError(result.Error);
			return json{ { "executable", result.Executable.string() }, { "files", result.FilesCopied } };
		});

		// ----------------------------------------------------------------- scene
		Register("scene.new", "", "Replace the edit scene with an empty one.", [&host](const json&) {
			host.StopPlay();
			host.SetEditScene(CreateRef<Scene>());
			return json::object();
		});

		Register("scene.open", "path", "Load a scene file (asset-relative) into the editor.", [&host](const json& args) {
			const std::string path = RequireString(args, "path");
			Ref<Scene> scene = CreateRef<Scene>();
			std::string error;
			if (!SceneSerializer::Deserialize(*scene, Project::ResolveAssetPath(path), error))
				throw CommandError(error);
			host.StopPlay();
			host.SetEditScene(scene);
			return json{ { "entities", scene->GetEntityCount() } };
		});

		Register("scene.save", "path?", "Save the edit scene (to its file, or to path).", [&host](const json& args) {
			Ref<Scene> scene = host.GetEditScene();
			std::filesystem::path path = scene->GetFilePath();
			if (args.contains("path"))
				path = ResolveInsideAssets(RequireString(args, "path"));
			if (path.empty())
				throw CommandError("the scene has no file yet: pass 'path'");
			if (!SceneSerializer::Serialize(*scene, path))
				throw CommandError(std::format("cannot write '{}'", path.string()));
			return json{ { "path", Project::MakeAssetRelative(path) } };
		});

		Register("scene.get", "", "The whole active scene as a scene document.", [&host](const json&) {
			return SceneSerializer::SerializeToJson(*RequireScene(host));
		});

		Register("scene.list", "component?", "Entities of the active scene (optionally only those with a component).", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			const std::string component = Optional<std::string>(args, "component", "");
			const ComponentInfo* info = component.empty() ? nullptr : ComponentRegistry::Find(component);
			if (!component.empty() && !info)
				throw CommandError(std::format("unknown component '{}'", component));
			json entities = json::array();
			for (Entity entity : scene->GetAllEntities())
			{
				if (info && !info->Has(entity))
					continue;
				json components = json::array();
				for (const ComponentInfo& candidate : ComponentRegistry::GetAll())
				{
					if (candidate.Has(entity))
						components.push_back(candidate.Name);
				}
				json item = { { "id", IdString(entity) }, { "name", entity.GetName() }, { "components", components } };
				if (Entity parent = entity.GetParent())
					item["parent"] = IdString(parent);
				entities.push_back(item);
			}
			return entities;
		});

		// ----------------------------------------------------------------- entities
		Register("entity.create", "name?, parent?, components?", "Create an entity; components is { \"Name\": { fields } }.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			Entity entity = scene->CreateEntity(Optional<std::string>(args, "name", "Entity"));
			try
			{
				if (args.contains("parent"))
					scene->SetParent(entity, RequireEntity(*scene, args, "parent", "parentName"), false);
				if (args.contains("components"))
					ApplyComponents(entity, args["components"]);
			}
			catch (...)
			{
				scene->DestroyEntity(entity);
				throw;
			}
			host.SetSelection(entity.GetUUID());
			return json{ { "id", IdString(entity) } };
		});

		Register("entity.get", "id|name, runtime?", "Full description of an entity (runtime state with runtime=true).", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			return DescribeEntity(RequireEntity(*scene, args), Optional<bool>(args, "runtime", scene->IsRunning()));
		});

		Register("entity.set", "id|name, components", "Add/merge components; absent fields keep their values.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			Entity entity = RequireEntity(*scene, args);
			ApplyComponents(entity, Require(args, "components"));
			return json::object();
		});

		Register("entity.remove_component", "id|name, component", "Remove a component from an entity.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			Entity entity = RequireEntity(*scene, args);
			const std::string name = RequireString(args, "component");
			const ComponentInfo* info = ComponentRegistry::Find(name);
			if (!info)
				throw CommandError(std::format("unknown component '{}'", name));
			if (!info->Removable)
				throw CommandError(std::format("component '{}' cannot be removed", name));
			if (info->Has(entity))
				info->Remove(entity);
			return json::object();
		});

		Register("entity.destroy", "id|name", "Destroy an entity and its children.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			scene->DestroyEntity(RequireEntity(*scene, args));
			return json::object();
		});

		Register("entity.duplicate", "id|name", "Duplicate an entity with its children.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			return json{ { "id", IdString(scene->DuplicateEntity(RequireEntity(*scene, args))) } };
		});

		Register("entity.set_parent", "id|name, parent|parentName (null to unparent), keepWorld?", "Re-parent an entity.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			Entity entity = RequireEntity(*scene, args);
			const bool keepWorld = Optional<bool>(args, "keepWorld", true);
			if ((args.contains("parent") && args["parent"].is_null()) || (!args.contains("parent") && !args.contains("parentName")))
				scene->Unparent(entity, keepWorld);
			else
				scene->SetParent(entity, RequireEntity(*scene, args, "parent", "parentName"), keepWorld);
			return json::object();
		});

		Register("entity.select", "id|name", "Select an entity in the editor.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			host.SetSelection(RequireEntity(*scene, args).GetUUID());
			return json::object();
		});

		Register("component.list", "", "Every component type with its fields and default values.", [](const json&) {
			json components = json::object();
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
				components[info.Name] = { { "removable", info.Removable }, { "defaults", info.Defaults() } };
			return components;
		});

		// ----------------------------------------------------------------- prefabs and assets
		Register("prefab.save", "id|name, path", "Save an entity tree as a prefab file.", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			Entity entity = RequireEntity(*scene, args);
			const std::filesystem::path path = ResolveInsideAssets(RequireString(args, "path"));
			if (!Prefab::Save(entity, path))
				throw CommandError("cannot write the prefab");
			return json{ { "path", Project::MakeAssetRelative(path) } };
		});

		Register("prefab.instantiate", "path, position?, rotation?", "Instantiate a prefab (rotation in Euler degrees).", [&host](const json& args) {
			Ref<Scene> scene = RequireScene(host);
			std::string error;
			Entity root = Prefab::Instantiate(*scene, RequireString(args, "path"), OptionalVec3(args, "position", glm::vec3(0.0f)),
				Math::QuatFromEulerDegrees(OptionalVec3(args, "rotation", glm::vec3(0.0f))), &error);
			if (!root)
				throw CommandError(error);
			return json{ { "id", IdString(root) } };
		});

		Register("asset.list", "directory?", "Files under the asset directory (or a subdirectory).", [](const json& args) {
			const std::filesystem::path directory = ResolveInsideAssets(Optional<std::string>(args, "directory", "."));
			json files = json::array();
			std::error_code error;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error))
			{
				if (entry.is_regular_file())
					files.push_back(Project::MakeAssetRelative(entry.path()));
			}
			return files;
		});

		Register("asset.read", "path", "Read a text asset (script, scene, prefab, ...).", [](const json& args) {
			const std::optional<std::string> text = FileSystem::ReadTextFile(ResolveInsideAssets(RequireString(args, "path")));
			if (!text)
				throw CommandError("cannot read the file");
			return json(*text);
		});

		Register("asset.write", "path, content", "Create or overwrite a text asset (scripts, scenes, prefabs).", [](const json& args) {
			const std::filesystem::path path = ResolveInsideAssets(RequireString(args, "path"));
			if (!FileSystem::WriteTextFile(path, RequireString(args, "content")))
				throw CommandError("cannot write the file");
			AssetManager::ReloadModified();
			return json{ { "path", Project::MakeAssetRelative(path) } };
		});

		Register("asset.import", "source, destination", "Copy an external file (model, texture, sound, HDRI) into the assets.", [](const json& args) {
			const std::filesystem::path source = RequireString(args, "source");
			const std::filesystem::path destination = ResolveInsideAssets(RequireString(args, "destination"));
			std::error_code error;
			std::filesystem::create_directories(destination.parent_path(), error);
			std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
			if (error)
				throw CommandError(std::format("cannot copy '{}': {}", source.string(), error.message()));
			// glTF files reference buffers/textures next to them: copy those too.
			if (source.extension() == ".gltf")
			{
				const json gltf = json::parse(FileSystem::ReadTextFile(source).value_or("{}"), nullptr, false);
				for (const char* section : { "buffers", "images" })
				{
					if (gltf.is_discarded() || !gltf.contains(section))
						continue;
					for (const json& item : gltf[section])
					{
						const std::string uri = item.value("uri", "");
						if (uri.empty() || uri.starts_with("data:"))
							continue;
						std::filesystem::create_directories((destination.parent_path() / uri).parent_path(), error);
						std::filesystem::copy_file(source.parent_path() / uri, destination.parent_path() / uri,
							std::filesystem::copy_options::overwrite_existing, error);
					}
				}
			}
			AssetManager::ReloadModified();
			return json{ { "path", Project::MakeAssetRelative(destination) } };
		});

		// ----------------------------------------------------------------- play mode
		Register("play", "", "Enter play mode (a runtime copy of the edit scene starts).", [&host](const json&) {
			if (!host.StartPlay())
				throw CommandError("already playing");
			return json::object();
		});

		Register("stop", "", "Leave play mode (edit scene is untouched).", [&host](const json&) {
			host.StopPlay();
			Input::Reset();
			return json::object();
		});

		Register("pause", "paused", "Pause or resume play mode.", [&host](const json& args) {
			Ref<Scene> scene = host.GetPlayScene();
			if (!scene)
				throw CommandError("not playing");
			scene->SetPaused(Optional<bool>(args, "paused", true));
			return json::object();
		});

		Register("simulate", "seconds?, frames?, dt?", "Advance play mode deterministically (default dt 1/60).", [&host](const json& args) {
			Ref<Scene> scene = host.GetPlayScene();
			if (!scene)
				throw CommandError("not playing (send 'play' first)");
			const float dt = Optional<float>(args, "dt", 1.0f / 60.0f);
			if (dt <= 0.0f || dt > 0.5f)
				throw CommandError("dt must be in (0, 0.5]");
			int frames = Optional<int>(args, "frames", 0);
			if (frames <= 0)
				frames = static_cast<int>(std::ceil(Optional<float>(args, "seconds", dt) / dt));
			frames = std::min(frames, 600000);
			const bool wasPaused = scene->IsPaused();
			scene->SetPaused(false);
			for (int i = 0; i < frames; i++)
			{
				scene->OnUpdateRuntime(Timestep(dt));
				Input::EndFrame();
				if (scene->GetRequestedQuit())
					break;
			}
			scene->SetPaused(wasPaused);
			const TestReport& report = scene->GetTestReport();
			return json{ { "time", scene->GetTime() }, { "frames", scene->GetFrameCount() },
				{ "scriptErrors", scene->GetScriptEngine() ? scene->GetScriptEngine()->GetErrors().size() : 0 },
				{ "testCompleted", report.Completed }, { "testPassed", report.Passed } };
		});

		Register("input.key", "key, down", "Inject a key state (e.g. {\"key\":\"W\",\"down\":true}).", [](const json& args) {
			const std::string name = RequireString(args, "key");
			const std::optional<KeyCode> key = KeyCodeFromName(name);
			if (!key)
				throw CommandError(std::format("unknown key '{}'", name));
			Input::SetKeyState(*key, Optional<bool>(args, "down", true));
			return json::object();
		});

		Register("input.mouse", "button?, down?, x?, y?", "Inject mouse button state and/or position.", [](const json& args) {
			if (args.contains("button"))
			{
				const std::optional<MouseCode> button = MouseCodeFromName(RequireString(args, "button"));
				if (!button)
					throw CommandError("unknown mouse button (Left, Right, Middle)");
				Input::SetMouseButtonState(*button, Optional<bool>(args, "down", true));
			}
			if (args.contains("x") || args.contains("y"))
			{
				const glm::vec2 current = Input::GetMousePosition();
				Input::SetMousePosition({ Optional<float>(args, "x", current.x), Optional<float>(args, "y", current.y) });
			}
			return json::object();
		});

		Register("script.exec", "code", "Run Lua in the playing scene; returns the chunk's result.", [&host](const json& args) {
			Ref<Scene> scene = host.GetPlayScene();
			if (!scene || !scene->GetScriptEngine())
				throw CommandError("not playing (send 'play' first)");
			std::string output;
			if (!scene->GetScriptEngine()->Execute(RequireString(args, "code"), output))
				throw CommandError(output);
			return output.empty() ? json() : json::parse(output, nullptr, false);
		});

		Register("script.errors", "", "Script errors raised in the current play session.", [&host](const json&) {
			Ref<Scene> scene = host.GetPlayScene();
			json errors = json::array();
			if (scene && scene->GetScriptEngine())
			{
				for (const ScriptError& error : scene->GetScriptEngine()->GetErrors())
					errors.push_back({ { "entity", std::to_string(static_cast<uint64_t>(error.Entity)) }, { "script", error.Script }, { "message", error.Message } });
			}
			return errors;
		});

		// ----------------------------------------------------------------- verification
		Register("render.screenshot", "path, width?, height?, camera?",
			"Render the active scene to a PNG. camera = { position:[x,y,z], target:[x,y,z], fov? } overrides the scene camera.",
			[&host](const json& args) {
				SceneRenderer* renderer = host.GetRenderer();
				RenderDevice* device = host.GetRenderDevice();
				if (!renderer || !device)
					throw CommandError("rendering is not available on this machine");
				Ref<Scene> scene = RequireScene(host);
				const uint32_t width = std::clamp(Optional<uint32_t>(args, "width", 1280), 16u, 8192u);
				const uint32_t height = std::clamp(Optional<uint32_t>(args, "height", 720), 16u, 8192u);
				const uint32_t previousWidth = renderer->GetViewportWidth(), previousHeight = renderer->GetViewportHeight();
				renderer->SetViewportSize(width, height);

				const float aspect = static_cast<float>(width) / static_cast<float>(height);
				RenderCamera camera = RenderCamera::FromScene(*scene, aspect);
				if (args.contains("camera"))
				{
					const json& cameraArgs = args["camera"];
					const glm::vec3 position = OptionalVec3(cameraArgs, "position", camera.Position);
					const glm::vec3 target = OptionalVec3(cameraArgs, "target", position + glm::vec3(0.0f, 0.0f, -1.0f));
					camera = RenderCamera::Perspective(position, Math::LookRotation(target - position), Optional<float>(cameraArgs, "fov", 60.0f),
						aspect, 0.05f, 500.0f);
				}

				const std::filesystem::path path = RequireString(args, "path");
				if (!device->BeginFrame())
					throw CommandError("the GPU device is lost");
				renderer->Render(*scene, camera);
				device->EndFrame();
				const bool saved = renderer->SaveScreenshot(path);
				renderer->SetViewportSize(previousWidth, previousHeight);
				if (!saved)
					throw CommandError(std::format("cannot write '{}'", path.string()));
				return json{ { "path", std::filesystem::absolute(path).string() }, { "drawCalls", renderer->GetStatistics().DrawCalls } };
			});

		Register("test.run", "scene, seconds?, dt?", "Load a scene, play it until its scripts call test.complete() (or timeout).", [&host](const json& args) {
			Ref<Scene> scene = CreateRef<Scene>();
			std::string error;
			if (!SceneSerializer::Deserialize(*scene, Project::ResolveAssetPath(RequireString(args, "scene")), error))
				throw CommandError(error);
			host.StopPlay();
			host.SetEditScene(scene);
			host.StartPlay();
			Ref<Scene> play = host.GetPlayScene();
			const float dt = Optional<float>(args, "dt", 1.0f / 60.0f);
			const float seconds = Optional<float>(args, "seconds", 60.0f);
			while (play->GetTime() < seconds && !play->GetTestReport().Completed)
			{
				play->OnUpdateRuntime(Timestep(dt));
				Input::EndFrame();
			}
			const TestReport report = play->GetTestReport();
			json errors = json::array();
			for (const ScriptError& scriptError : play->GetScriptEngine()->GetErrors())
				errors.push_back(scriptError.Script + ": " + scriptError.Message);
			host.StopPlay();
			return json{ { "completed", report.Completed }, { "passed", report.Completed && report.Passed && errors.empty() },
				{ "assertions", report.Assertions }, { "failures", report.Failures }, { "messages", report.Messages }, { "scriptErrors", errors } };
		});
	}

}
