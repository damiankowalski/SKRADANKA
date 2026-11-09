#include "pnpch.h"
#include "Penumbra/Scene/SceneSerializer.h"

#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/JsonSerialization.h"

namespace Penumbra {

	using json = nlohmann::json;

	namespace {

		void AppendEntityRecursive(Entity entity, json& entities)
		{
			json record = json::object();
			ToJson(record["ID"], entity.GetUUID());
			if (Entity parent = entity.GetParent())
				ToJson(record["Parent"], parent.GetUUID());
			record["Components"] = ComponentRegistry::SerializeEntityComponents(entity);
			entities.push_back(std::move(record));

			for (Entity child : entity.GetChildren())
				AppendEntityRecursive(child, entities);
		}

		struct ParsedEntity
		{
			UUID ID = UUID::Invalid;
			UUID Parent = UUID::Invalid;
			const json* Components = nullptr;
		};

		bool ParseEntities(const json& document, const char* expectedFormat, std::vector<ParsedEntity>& parsed, std::string& error)
		{
			if (!document.is_object())
			{
				error = "document must be a JSON object";
				return false;
			}
			const auto format = document.find("Format");
			if (format == document.end() || !format->is_string() || format->get<std::string>() != expectedFormat)
			{
				error = std::format("'Format' must be \"{}\"", expectedFormat);
				return false;
			}
			const auto version = document.find("Version");
			if (version == document.end() || !version->is_number_integer() || version->get<int>() > SceneSerializer::Version)
			{
				error = std::format("unsupported 'Version' (this engine reads up to {})", SceneSerializer::Version);
				return false;
			}
			const auto entities = document.find("Entities");
			if (entities == document.end() || !entities->is_array())
			{
				error = "'Entities' must be an array";
				return false;
			}

			std::unordered_set<uint64_t> seen;
			for (size_t i = 0; i < entities->size(); i++)
			{
				const json& record = (*entities)[i];
				ParsedEntity entity;
				std::string fieldError;
				if (!record.is_object() || !record.contains("ID") || !ParseUUID(record["ID"], entity.ID, fieldError) || !entity.ID.IsValid())
				{
					error = std::format("Entities[{}]: missing or invalid 'ID'", i);
					return false;
				}
				if (!seen.insert(entity.ID).second)
				{
					error = std::format("Entities[{}]: duplicate ID {}", i, static_cast<uint64_t>(entity.ID));
					return false;
				}
				if (record.contains("Parent") && !ParseUUID(record["Parent"], entity.Parent, fieldError))
				{
					error = std::format("Entities[{}]: invalid 'Parent'", i);
					return false;
				}
				const auto components = record.find("Components");
				if (components == record.end() || !components->is_object())
				{
					error = std::format("Entities[{}]: 'Components' must be an object", i);
					return false;
				}
				entity.Components = &*components;
				parsed.push_back(entity);
			}

			for (size_t i = 0; i < parsed.size(); i++)
			{
				if (parsed[i].Parent.IsValid() && !seen.contains(parsed[i].Parent))
				{
					error = std::format("Entities[{}]: parent {} does not exist", i, static_cast<uint64_t>(parsed[i].Parent));
					return false;
				}
			}
			return true;
		}

		// Creates the parsed entities in the scene, remapping IDs through idMap.
		bool CreateEntities(Scene& scene, const std::vector<ParsedEntity>& parsed, const std::unordered_map<uint64_t, UUID>& idMap,
			std::vector<Entity>& created, std::string& error)
		{
			for (const ParsedEntity& record : parsed)
			{
				Entity entity = scene.CreateEntityWithUUID(idMap.at(record.ID));
				created.push_back(entity);
				std::string componentError;
				if (!ComponentRegistry::DeserializeEntityComponents(entity, *record.Components, componentError))
				{
					error = std::format("entity {}: {}", static_cast<uint64_t>(record.ID), componentError);
					return false;
				}
			}
			for (size_t i = 0; i < parsed.size(); i++)
			{
				if (!parsed[i].Parent.IsValid())
					continue;
				Entity parent = scene.GetEntityByUUID(idMap.at(parsed[i].Parent));
				if (scene.IsAncestor(created[i], parent) || created[i] == parent)
				{
					error = std::format("entity {}: parent cycle", static_cast<uint64_t>(parsed[i].ID));
					return false;
				}
				scene.SetParent(created[i], parent, false);
			}
			return true;
		}

		struct PrefabCacheEntry
		{
			std::filesystem::file_time_type WriteTime;
			json Document;
		};

		std::unordered_map<std::string, PrefabCacheEntry>& GetPrefabCache()
		{
			static std::unordered_map<std::string, PrefabCacheEntry> s_Cache;
			return s_Cache;
		}

	}

	json SceneSerializer::SerializeToJson(Scene& scene)
	{
		json document = json::object();
		document["Format"] = "PenumbraScene";
		document["Version"] = Version;
		json entities = json::array();
		for (Entity root : scene.GetRootEntities())
			AppendEntityRecursive(root, entities);
		document["Entities"] = std::move(entities);
		return document;
	}

	bool SceneSerializer::Serialize(Scene& scene, const std::filesystem::path& path)
	{
		const bool ok = FileSystem::WriteTextFile(path, SerializeToJson(scene).dump(2) + "\n");
		if (ok)
			scene.SetFilePath(path);
		else
			PN_CORE_ERROR("Failed to write scene '{}'", path.string());
		return ok;
	}

	bool SceneSerializer::DeserializeFromJson(Scene& scene, const json& document, std::string& error)
	{
		PN_CORE_ASSERT(scene.GetEntityCount() == 0, "Deserialize requires an empty scene");

		std::vector<ParsedEntity> parsed;
		if (!ParseEntities(document, "PenumbraScene", parsed, error))
			return false;

		std::unordered_map<uint64_t, UUID> identity;
		for (const ParsedEntity& record : parsed)
			identity[record.ID] = record.ID;

		std::vector<Entity> created;
		if (!CreateEntities(scene, parsed, identity, created, error))
		{
			for (Entity entity : scene.GetAllEntities())
			{
				if (entity.IsValid())
					scene.DestroyEntity(entity);
			}
			return false;
		}
		return true;
	}

	bool SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& path, std::string& error)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
		{
			error = std::format("cannot read '{}'", path.string());
			return false;
		}
		const json document = json::parse(*text, nullptr, false);
		if (document.is_discarded())
		{
			error = std::format("'{}' is not valid JSON", path.string());
			return false;
		}
		if (!DeserializeFromJson(scene, document, error))
		{
			error = std::format("'{}': {}", path.string(), error);
			return false;
		}
		scene.SetFilePath(path);
		return true;
	}

	json SceneSerializer::SerializeEntityTree(Entity root)
	{
		json document = json::object();
		document["Format"] = "PenumbraPrefab";
		document["Version"] = Version;
		json entities = json::array();
		AppendEntityRecursive(root, entities);
		entities[0].erase("Parent");
		document["Entities"] = std::move(entities);
		return document;
	}

	Entity SceneSerializer::InstantiateEntityTree(Scene& scene, const json& document, std::string& error)
	{
		std::vector<ParsedEntity> parsed;
		if (!ParseEntities(document, "PenumbraPrefab", parsed, error))
			return {};
		if (parsed.empty() || parsed[0].Parent.IsValid())
		{
			error = "prefab must start with its root entity (no parent)";
			return {};
		}

		std::unordered_map<uint64_t, UUID> fresh;
		for (const ParsedEntity& record : parsed)
			fresh[record.ID] = UUID();

		std::vector<Entity> created;
		if (!CreateEntities(scene, parsed, fresh, created, error))
		{
			for (Entity entity : created)
			{
				if (entity.IsValid() && !entity.GetParent())
					scene.DestroyEntity(entity);
			}
			return {};
		}
		return created[0];
	}

	bool Prefab::Save(Entity root, const std::filesystem::path& path)
	{
		const bool ok = FileSystem::WriteTextFile(path, SceneSerializer::SerializeEntityTree(root).dump(2) + "\n");
		if (!ok)
			PN_CORE_ERROR("Failed to write prefab '{}'", path.string());
		GetPrefabCache().erase(path.lexically_normal().generic_string());
		return ok;
	}

	Entity Prefab::Instantiate(Scene& scene, const std::filesystem::path& path, const glm::vec3& position, const glm::quat& rotation,
		std::string* errorOut)
	{
		const std::filesystem::path resolved = Project::ResolveAssetPath(path);
		const std::string key = resolved.lexically_normal().generic_string();

		std::string error;
		std::error_code fsError;
		const auto writeTime = std::filesystem::last_write_time(resolved, fsError);
		if (fsError)
		{
			error = std::format("prefab '{}' not found", path.generic_string());
		}
		else
		{
			auto& cache = GetPrefabCache();
			auto it = cache.find(key);
			if (it == cache.end() || it->second.WriteTime != writeTime)
			{
				const std::optional<std::string> text = FileSystem::ReadTextFile(resolved);
				json document = text ? json::parse(*text, nullptr, false) : json(json::value_t::discarded);
				if (document.is_discarded())
					error = std::format("prefab '{}' is not valid JSON", path.generic_string());
				else
					it = cache.insert_or_assign(key, PrefabCacheEntry{ writeTime, std::move(document) }).first;
			}

			if (error.empty())
			{
				Entity root = SceneSerializer::InstantiateEntityTree(scene, it->second.Document, error);
				if (root)
				{
					auto& transform = root.GetComponent<TransformComponent>();
					transform.Translation = position;
					transform.Rotation = rotation;
					if (!root.HasComponent<PrefabComponent>())
						root.AddComponent<PrefabComponent>();
					root.GetComponent<PrefabComponent>().Prefab = path.generic_string();
					return root;
				}
				error = std::format("prefab '{}': {}", path.generic_string(), error);
			}
		}

		PN_CORE_ERROR("{}", error);
		if (errorOut)
			*errorOut = error;
		return {};
	}

	void Prefab::ClearCache() { GetPrefabCache().clear(); }

}
