#include "TestHelpers.h"

#include "Penumbra/Automation/CommandProcessor.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Project/ProjectExporter.h"

using namespace Penumbra;
using json = nlohmann::json;

namespace {

	struct AutomationFixture
	{
		std::filesystem::path Directory = std::filesystem::temp_directory_path() / "PenumbraTests" / std::to_string(static_cast<uint64_t>(UUID()));
		HeadlessAutomationHost Host;
		CommandProcessor Commands{ Host };

		AutomationFixture()
		{
			Host.SetUseWarp(true);
			Ok({ { "cmd", "project.new" }, { "path", (Directory / "Game").string() }, { "name", "Test Game" } });
		}

		~AutomationFixture()
		{
			Host.StopPlay();
			Project::Unload();
			std::error_code ignored;
			std::filesystem::remove_all(Directory, ignored);
		}

		json Ok(const json& request)
		{
			const json response = Commands.Execute(request);
			INFO(request.dump());
			INFO(response.dump());
			REQUIRE(response.value("ok", false));
			return response["result"];
		}

		std::string Fail(const json& request)
		{
			const json response = Commands.Execute(request);
			INFO(response.dump());
			REQUIRE_FALSE(response.value("ok", true));
			return response.value("error", std::string());
		}
	};

}

TEST_SUITE("Automation")
{
	TEST_CASE("Requests are validated")
	{
		AutomationFixture f;
		CHECK(f.Commands.Execute(json::array()).value("ok", true) == false);
		CHECK(f.Commands.Execute(json{ { "nope", 1 } }).value("ok", true) == false);
		CHECK(f.Fail({ { "cmd", "does.not.exist" } }).find("unknown command") != std::string::npos);
		CHECK(f.Fail({ { "cmd", "entity.get" } }).find("missing argument") != std::string::npos);
		CHECK(f.Ok({ { "cmd", "help" } }).size() == f.Commands.GetCommands().size());
		CHECK(f.Ok({ { "cmd", "component.list" } }).contains("Perception"));
	}

	TEST_CASE("An agent builds, saves, reloads and plays a scene")
	{
		AutomationFixture f;
		const json ground = f.Ok({ { "cmd", "entity.create" }, { "name", "Ground" },
			{ "components", { { "Transform", { { "Translation", { 0, -0.5, 0 } }, { "Scale", { 20, 1, 20 } } } }, { "Mesh", { { "Mesh", "builtin:cube" } } },
								{ "BoxCollider", json::object() } } } });
		const json ball = f.Ok({ { "cmd", "entity.create" }, { "name", "Ball" },
			{ "components", { { "Transform", { { "Translation", { 0, 4, 0 } } } }, { "Mesh", { { "Mesh", "builtin:sphere" } } },
								{ "SphereCollider", json::object() }, { "RigidBody", { { "Type", "Dynamic" } } }, { "Script", { { "Script", "Scripts/Ball.lua" } } } } } });
		f.Ok({ { "cmd", "asset.write" }, { "path", "Scripts/Ball.lua" },
			{ "content", "local Ball = {}\nfunction Ball:on_create() self.ticks = 0 end\nfunction Ball:on_update(dt) self.ticks = self.ticks + 1 end\nreturn Ball\n" } });

		CHECK(f.Fail({ { "cmd", "entity.create" }, { "components", { { "Mesh", { { "Msh", "x" } } } } } }).find("unknown field") != std::string::npos);
		CHECK(f.Ok({ { "cmd", "scene.list" } }).size() == 2);

		f.Ok({ { "cmd", "entity.set" }, { "name", "Ball" }, { "components", { { "Material", { { "BaseColor", { 1, 0, 0, 1 } } } } } } });
		CHECK(f.Ok({ { "cmd", "entity.get" }, { "id", ball["id"] } })["components"]["Material"]["BaseColor"][0] == 1.0);

		f.Ok({ { "cmd", "entity.set_parent" }, { "name", "Ball" }, { "parent", ground["id"] } });
		CHECK(f.Ok({ { "cmd", "entity.get" }, { "name", "Ball" } })["parent"] == ground["id"]);
		f.Ok({ { "cmd", "entity.set_parent" }, { "name", "Ball" }, { "parent", nullptr } });

		f.Ok({ { "cmd", "scene.save" }, { "path", "Scenes/Main.pscene" } });
		CHECK(f.Fail({ { "cmd", "scene.save" }, { "path", "../../outside.pscene" } }).find("outside") != std::string::npos);
		f.Ok({ { "cmd", "scene.new" } });
		CHECK(f.Ok({ { "cmd", "scene.list" } }).empty());
		f.Ok({ { "cmd", "scene.open" }, { "path", "Scenes/Main.pscene" } });
		CHECK(f.Ok({ { "cmd", "scene.list" } }).size() == 2);

		f.Ok({ { "cmd", "play" } });
		CHECK(f.Fail({ { "cmd", "play" } }).find("already") != std::string::npos);
		const json simulated = f.Ok({ { "cmd", "simulate" }, { "seconds", 2.0 } });
		CHECK(simulated["frames"] == 120);
		CHECK(simulated["scriptErrors"] == 0);
		CHECK(f.Ok({ { "cmd", "entity.get" }, { "name", "Ball" } })["worldPosition"][1].get<float>() < 1.0f);
		CHECK(f.Ok({ { "cmd", "script.exec" }, { "code", "return scene.find('Ball'):script().ticks" } }) == 120);
		f.Ok({ { "cmd", "input.key" }, { "key", "W" }, { "down", true } });
		CHECK(Input::IsKeyDown(Key::W));
		f.Ok({ { "cmd", "stop" } });
		CHECK_FALSE(Input::IsKeyDown(Key::W));
		// The edit scene is untouched by play mode.
		CHECK(f.Ok({ { "cmd", "entity.get" }, { "name", "Ball" } })["worldPosition"][1] == 4.0);
		CHECK(f.Fail({ { "cmd", "simulate" } }).find("not playing") != std::string::npos);
	}

	TEST_CASE("Prefabs, assets, batches and scripted tests")
	{
		AutomationFixture f;
		f.Ok({ { "cmd", "entity.create" }, { "name", "Torch" }, { "components", { { "PointLight", { { "Intensity", 5 } } } } } });
		f.Ok({ { "cmd", "prefab.save" }, { "name", "Torch" }, { "path", "Prefabs/Torch.pprefab" } });
		const json instance = f.Ok({ { "cmd", "prefab.instantiate" }, { "path", "Prefabs/Torch.pprefab" }, { "position", { 3, 0, 0 } } });
		CHECK(f.Ok({ { "cmd", "entity.get" }, { "id", instance["id"] } })["components"]["PointLight"]["Intensity"] == 5.0);
		const json files = f.Ok({ { "cmd", "asset.list" } });
		CHECK(std::find(files.begin(), files.end(), "Prefabs/Torch.pprefab") != files.end());
		CHECK(f.Ok({ { "cmd", "asset.read" }, { "path", "Prefabs/Torch.pprefab" } }).get<std::string>().find("PenumbraPrefab") != std::string::npos);

		const json batch = f.Commands.ExecuteBatch("# comment\n{\"cmd\":\"scene.new\"}\n{\"cmd\":\"entity.create\",\"name\":\"A\"}\n{\"cmd\":\"bad\"}\n{\"cmd\":\"entity.create\"}\n");
		REQUIRE(batch.size() == 3); // stops at the first failure
		CHECK(batch[2]["ok"] == false);

		f.Ok({ { "cmd", "asset.write" }, { "path", "Scripts/Check.lua" },
			{ "content", "local T = {}\nfunction T:on_update(dt)\n if time.elapsed() > 0.5 then test.check(1 + 1 == 2, 'math'); test.complete() end\nend\nreturn T\n" } });
		f.Ok({ { "cmd", "scene.new" } });
		f.Ok({ { "cmd", "entity.create" }, { "name", "Runner" }, { "components", { { "Script", { { "Script", "Scripts/Check.lua" } } } } } });
		f.Ok({ { "cmd", "scene.save" }, { "path", "Scenes/Check.pscene" } });
		const json report = f.Ok({ { "cmd", "test.run" }, { "scene", "Scenes/Check.pscene" }, { "seconds", 5 } });
		CHECK(report["completed"] == true);
		CHECK(report["passed"] == true);
		CHECK(report["assertions"] == 1);
	}

	TEST_CASE("Export produces a self-contained game folder")
	{
		AutomationFixture f;
		f.Ok({ { "cmd", "scene.save" }, { "path", "Scenes/Main.pscene" } });
		const std::filesystem::path fakeRuntime = f.Directory / "PenumbraRuntime.exe";
		REQUIRE(FileSystem::WriteTextFile(fakeRuntime, "MZ"));
		f.Host.SetRuntimeExecutable(fakeRuntime);

		const json result = f.Ok({ { "cmd", "project.export" }, { "output", (f.Directory / "Out").string() } });
		CHECK(std::filesystem::exists(f.Directory / "Out" / "Test Game.exe"));
		CHECK(std::filesystem::exists(f.Directory / "Out" / "project.json"));
		CHECK(std::filesystem::exists(f.Directory / "Out" / "Assets" / "Scenes" / "Main.pscene"));

		const ExportResult missing = ProjectExporter::Export(Project::GetProjectDirectory(), f.Directory / "Out2", f.Directory / "missing.exe");
		CHECK_FALSE(missing.Success);
		CHECK(missing.Error.find("runtime executable") != std::string::npos);
		const ExportResult inside = ProjectExporter::Export(Project::GetProjectDirectory(), Project::GetAssetDirectory() / "Export", fakeRuntime);
		CHECK_FALSE(inside.Success);
	}

	TEST_CASE("Screenshots render through the headless host")
	{
		AutomationFixture f;
		f.Ok({ { "cmd", "entity.create" }, { "name", "Sun" }, { "components", { { "DirectionalLight", json::object() } } } });
		f.Ok({ { "cmd", "entity.create" }, { "name", "Box" }, { "components", { { "Mesh", json::object() } } } });
		const std::filesystem::path path = f.Directory / "shot.png";
		f.Ok({ { "cmd", "render.screenshot" }, { "path", path.string() }, { "width", 160 }, { "height", 90 },
			{ "camera", { { "position", { 3, 2, 3 } }, { "target", { 0, 0, 0 } } } } });
		CHECK(std::filesystem::file_size(path) > 100);
	}
}
