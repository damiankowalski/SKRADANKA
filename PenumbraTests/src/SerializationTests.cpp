#include "TestHelpers.h"

#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/SceneSerializer.h"

using namespace Penumbra;
using json = nlohmann::json;

TEST_SUITE("Serialization")
{
	TEST_CASE("Every registered component round-trips its defaults")
	{
		Scene scene;
		Entity entity = scene.CreateEntity("All");
		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			CAPTURE(info.Name);
			info.Add(entity);
			REQUIRE(info.Has(entity));
			const json serialized = info.Serialize(entity, {});
			CHECK(serialized.is_object());
			if (info.Removable)
				CHECK(serialized == info.Defaults());
			std::string error;
			CHECK_MESSAGE(info.Deserialize(entity, serialized, error), error);
			CHECK(info.Serialize(entity, {}) == serialized);
		}
	}

	TEST_CASE("Scene round-trips through JSON with hierarchy and all data")
	{
		Scene scene;
		Entity root = scene.CreateEntity("Room");
		root.Transform().Translation = { 1.0f, 2.0f, 3.0f };
		root.Transform().SetEulerDegrees({ 0.0f, 45.0f, 0.0f });
		root.AddComponent<MeshComponent>().Mesh = "builtin:sphere";
		auto& light = root.AddComponent<PointLightComponent>();
		light.Intensity = 7.5f;
		light.Color = { 1.0f, 0.5f, 0.25f };
		auto& response = root.AddComponent<StimResponseComponent>();
		response.Rules.push_back({ "Water", 0.25f, true, { { StimActionType::DisableLights, "", 0.0f, 0.0f }, { StimActionType::SpawnPrefab, "Prefabs/Smoke.pprefab", 0.0f, 0.0f } } });
		auto& script = root.AddComponent<ScriptComponent>();
		script.Script = "Scripts/Torch.lua";
		script.Properties["speed"] = 2.5;
		script.Properties["label"] = std::string("hello");
		script.Properties["offset"] = glm::vec3(1.0f, 2.0f, 3.0f);
		script.Properties["enabled"] = true;

		Entity child = scene.CreateEntity("Flame");
		scene.SetParent(child, root, false);
		Entity second = scene.CreateEntity("SecondChild");
		scene.SetParent(second, root, false);

		const json document = SceneSerializer::SerializeToJson(scene);
		Scene loaded;
		std::string error;
		REQUIRE_MESSAGE(SceneSerializer::DeserializeFromJson(loaded, document, error), error);

		Entity loadedRoot = loaded.GetEntityByUUID(root.GetUUID());
		CHECK(loadedRoot.GetName() == "Room");
		CHECK(Test::Near(loadedRoot.Transform().Translation, { 1.0f, 2.0f, 3.0f }));
		CHECK(Test::Near(loadedRoot.Transform().GetEulerDegrees(), { 0.0f, 45.0f, 0.0f }, 1e-3f));
		CHECK(loadedRoot.GetComponent<MeshComponent>().Mesh == "builtin:sphere");
		CHECK(loadedRoot.GetComponent<PointLightComponent>().Intensity == 7.5f);
		const auto& rules = loadedRoot.GetComponent<StimResponseComponent>().Rules;
		REQUIRE(rules.size() == 1);
		CHECK(rules[0].Stim == "Water");
		REQUIRE(rules[0].Actions.size() == 2);
		CHECK(rules[0].Actions[1].Type == StimActionType::SpawnPrefab);
		CHECK(rules[0].Actions[1].Asset == "Prefabs/Smoke.pprefab");
		const auto& properties = loadedRoot.GetComponent<ScriptComponent>().Properties;
		CHECK(std::get<double>(properties.at("speed")) == 2.5);
		CHECK(std::get<std::string>(properties.at("label")) == "hello");
		CHECK(std::get<glm::vec3>(properties.at("offset")) == glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK(std::get<bool>(properties.at("enabled")));

		// Child order is preserved.
		const auto children = loadedRoot.GetChildren();
		REQUIRE(children.size() == 2);
		CHECK(children[0].GetName() == "Flame");
		CHECK(children[1].GetName() == "SecondChild");

		// Serializing again yields the identical document (stable output).
		CHECK(SceneSerializer::SerializeToJson(loaded) == document);
	}

	TEST_CASE("Invalid documents are rejected with precise errors")
	{
		auto load = [](const json& document) {
			Scene scene;
			std::string error;
			const bool ok = SceneSerializer::DeserializeFromJson(scene, document, error);
			CHECK(scene.GetEntityCount() == (ok ? scene.GetEntityCount() : 0));
			return ok ? std::string() : error;
		};
		auto entityDoc = [](json components) {
			return json{ { "Format", "PenumbraScene" }, { "Version", 1 },
				{ "Entities", json::array({ { { "ID", "5" }, { "Components", std::move(components) } } }) } };
		};

		CHECK(load(json::object()).find("Format") != std::string::npos);
		CHECK(load(json{ { "Format", "PenumbraScene" }, { "Version", 99 }, { "Entities", json::array() } }).find("Version") != std::string::npos);
		CHECK(load(entityDoc({ { "Bogus", json::object() } })).find("unknown component 'Bogus'") != std::string::npos);
		CHECK(load(entityDoc({ { "PointLight", { { "Intensty", 2 } } } })).find("unknown field 'Intensty'") != std::string::npos);
		CHECK(load(entityDoc({ { "PointLight", { { "Intensity", "bright" } } } })).find("expected a number") != std::string::npos);
		CHECK(load(entityDoc({ { "RigidBody", { { "Type", "Floating" } } } })).find("expected one of") != std::string::npos);
		CHECK(load(entityDoc({ { "Transform", { { "Translation", json::array({ 1, 2 }) } } } })).find("array of 3") != std::string::npos);

		json duplicate = entityDoc(json::object());
		duplicate["Entities"].push_back(duplicate["Entities"][0]);
		CHECK(load(duplicate).find("duplicate ID") != std::string::npos);

		json orphan = entityDoc(json::object());
		orphan["Entities"][0]["Parent"] = "77";
		CHECK(load(orphan).find("does not exist") != std::string::npos);
	}

	TEST_CASE("Component merge keeps absent fields and rejects bad input atomically")
	{
		Scene scene;
		Entity entity = scene.CreateEntity("Light");
		auto& light = entity.AddComponent<SpotLightComponent>();
		light.Range = 33.0f;
		const ComponentInfo* info = ComponentRegistry::Find("SpotLight");
		REQUIRE(info);

		std::string error;
		REQUIRE(info->Deserialize(entity, json{ { "Intensity", 3.0 } }, error));
		CHECK(light.Range == 33.0f);
		CHECK(light.Intensity == 3.0f);

		CHECK_FALSE(info->Deserialize(entity, json{ { "Intensity", 9.0 }, { "Range", "far" } }, error));
		CHECK(light.Intensity == 3.0f); // unchanged: the whole merge failed
	}

	TEST_CASE("Runtime state is only serialized on request and never deserialized")
	{
		Scene scene;
		Entity guard = scene.CreateEntity("Guard");
		guard.AddComponent<PerceptionComponent>().RuntimeSuspicion = 0.5f;
		const ComponentInfo* info = ComponentRegistry::Find("Perception");
		CHECK_FALSE(info->Serialize(guard, {}).contains("Suspicion"));
		const json runtime = info->Serialize(guard, { .IncludeRuntimeState = true });
		CHECK(runtime.at("Suspicion") == 0.5f);
		CHECK(runtime.at("State") == "Unaware");
	}

	TEST_CASE("Scene files and prefabs on disk")
	{
		Test::TemporaryProject project;
		Scene scene;
		Entity keg = scene.CreateEntity("Keg");
		keg.AddComponent<MeshComponent>().Mesh = "builtin:cylinder";
		Entity fuse = scene.CreateEntity("Fuse");
		scene.SetParent(fuse, keg, false);
		fuse.Transform().Translation = { 0.0f, 1.0f, 0.0f };

		const std::filesystem::path scenePath = project.GetAssetDirectory() / "Scenes" / "Test.pscene";
		REQUIRE(SceneSerializer::Serialize(scene, scenePath));
		Scene loaded;
		std::string error;
		REQUIRE_MESSAGE(SceneSerializer::Deserialize(loaded, scenePath, error), error);
		CHECK(loaded.GetEntityCount() == 2);
		CHECK(loaded.GetFilePath() == scenePath);

		REQUIRE(Prefab::Save(keg, project.GetAssetDirectory() / "Prefabs" / "Keg.pprefab"));
		Entity a = Prefab::Instantiate(loaded, "Prefabs/Keg.pprefab", { 5.0f, 0.0f, 0.0f });
		Entity b = Prefab::Instantiate(loaded, "Prefabs/Keg.pprefab", { -5.0f, 0.0f, 0.0f });
		REQUIRE(a.IsValid());
		REQUIRE(b.IsValid());
		CHECK(a.GetUUID() != b.GetUUID());
		CHECK(a.GetUUID() != keg.GetUUID());
		CHECK(a.GetComponent<PrefabComponent>().Prefab == "Prefabs/Keg.pprefab");
		REQUIRE(a.GetChildren().size() == 1);
		CHECK(Test::Near(loaded.GetWorldPosition(a.GetChildren()[0]), { 5.0f, 1.0f, 0.0f }));
		CHECK(loaded.GetEntityCount() == 6);

		std::string prefabError;
		CHECK_FALSE(Prefab::Instantiate(loaded, "Prefabs/Missing.pprefab", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), &prefabError).IsValid());
		CHECK(prefabError.find("not found") != std::string::npos);
	}
}
