#include "TestHelpers.h"

#include "Penumbra/Animation/Animation.h"
#include "Penumbra/Animation/RagdollSystem.h"
#include "Penumbra/Asset/Importers.h"

using namespace Penumbra;

// The humanoid ragdoll built from the demo's KayKit rig (Animation/RagdollSystem). Scene-level
// behaviour (falling, settling, carrying) is in LevelTests.cpp.

TEST_SUITE("Animation")
{
	TEST_CASE("A humanoid ragdoll is built from the KayKit skeleton: 12 jointed parts sized to the rig")
	{
		std::string error;
		const std::optional<MeshData> knight =
			GltfImporter::Import(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "Assets/Models/Characters/Knight.glb", error);
		REQUIRE_MESSAGE(knight.has_value(), error);
		std::vector<glm::mat4> globals;
		Animation::ComputeGlobalTransforms(knight->Skeleton, Animation::RestPose(knight->Skeleton), globals);

		std::vector<RagdollPartDesc> parts;
		std::vector<int32_t> nodes;
		REQUIRE_MESSAGE(RagdollSystem::BuildHumanoid(knight->Skeleton, globals, 1.0f, 70.0f, parts, nodes, error), error);
		REQUIRE(parts.size() == 12);
		REQUIRE(nodes.size() == parts.size());
		CHECK(parts[0].Parent == -1);
		float mass = 0.0f;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			CHECK(parts[i].Parent < static_cast<int32_t>(i)); // parents first
			CHECK(parts[i].Radius > 0.0f);
			mass += parts[i].Mass;
		}
		CHECK(mass == doctest::Approx(70.0f));
		CHECK(knight->Skeleton.Nodes[static_cast<size_t>(nodes[3])].Name == "head");
		CHECK(parts[3].HalfHeight == 0.0f);                     // the head is a ball
		CHECK(parts[3].Position.y > parts[2].Position.y);       // above the chest
		CHECK(parts[9].Position.y < parts[8].Position.y);       // the shin below the thigh
		CHECK(parts[8].Position.x * parts[10].Position.x < 0.0f); // one leg each side

		// The same rig twice as large: twice the thickness.
		std::vector<glm::mat4> doubled = globals;
		for (glm::mat4& m : doubled)
			m = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)) * m;
		std::vector<RagdollPartDesc> large;
		REQUIRE(RagdollSystem::BuildHumanoid(knight->Skeleton, doubled, 1.0f, 70.0f, large, nodes, error));
		CHECK(large[4].Radius == doctest::Approx(parts[4].Radius * 2.0f));

		// A rig without the humanoid bones is refused with a message.
		Skeleton tail;
		tail.Nodes.push_back({ "root", -1, {} });
		CHECK_FALSE(RagdollSystem::BuildHumanoid(tail, { glm::mat4(1.0f) }, 1.0f, 70.0f, parts, nodes, error));
		CHECK(error.find("hips") != std::string::npos);
	}
}
