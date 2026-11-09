#pragma once

#include "Penumbra/Scene/Entity.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace Penumbra {

	// Scenes (.pscene) and prefabs (.pprefab) are JSON documents:
	// {
	//   "Format": "PenumbraScene", "Version": 1,
	//   "Entities": [ { "ID": "<uuid>", "Parent": "<uuid>", "Components": { "<Name>": { ... } } } ]
	// }
	// Parents always precede their children, which preserves child order on load.
	class SceneSerializer
	{
	public:
		static constexpr int Version = 1;

		static nlohmann::json SerializeToJson(Scene& scene);
		static bool Serialize(Scene& scene, const std::filesystem::path& path);

		// Loads into an empty scene. On failure the scene is left empty and error is set.
		static bool DeserializeFromJson(Scene& scene, const nlohmann::json& document, std::string& error);
		static bool Deserialize(Scene& scene, const std::filesystem::path& path, std::string& error);

		// Serializes an entity and all of its descendants (prefab / clipboard format).
		static nlohmann::json SerializeEntityTree(Entity root);
		// Creates the entities of a tree document with fresh UUIDs and returns the root.
		static Entity InstantiateEntityTree(Scene& scene, const nlohmann::json& document, std::string& error);
	};

	class Prefab
	{
	public:
		static bool Save(Entity root, const std::filesystem::path& path);
		// Instantiates a prefab file at a world position/rotation. Returns an invalid
		// entity (and logs) if the file is missing or malformed.
		static Entity Instantiate(Scene& scene, const std::filesystem::path& path, const glm::vec3& position,
			const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), std::string* error = nullptr);
		static void ClearCache();
	};

}
