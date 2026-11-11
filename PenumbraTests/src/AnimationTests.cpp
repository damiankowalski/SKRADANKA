#include "TestHelpers.h"

#include "Penumbra/Animation/Animation.h"
#include "Penumbra/Animation/AnimationSystem.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Asset/Importers.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/Scene.h"
#include "Penumbra/Scripting/ScriptEngine.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Penumbra;

namespace {

	MeshData ImportTestMesh(Test::TemporaryProject& project)
	{
		project.WriteAsset("Models/Bar.gltf", Test::MakeSkinnedGltf());
		std::string error;
		std::optional<MeshData> mesh = GltfImporter::Import(project.GetAssetDirectory() / "Models/Bar.gltf", error);
		REQUIRE_MESSAGE(mesh, error);
		return std::move(*mesh);
	}

	glm::vec3 SkinnedPosition(const MeshData& mesh, size_t vertex, const std::vector<glm::mat4>& skin)
	{
		return Animation::SkinVertexCPU(mesh.Vertices[vertex], mesh.SkinVertices[vertex], skin).Position;
	}

	std::vector<glm::mat4> PoseAt(const MeshData& mesh, const std::string& clip, float time)
	{
		AnimatorComponent animator;
		animator.Clip = clip;
		animator.RuntimeClip = clip;
		animator.RuntimeTime = time;
		std::vector<glm::mat4> skin;
		AnimationSystem::EvaluateSkinMatrices(mesh, &animator, skin);
		return skin;
	}

	AnimationChannel MakeChannel(AnimationPath path, AnimationInterpolation interpolation, std::vector<float> times, std::vector<glm::vec4> values)
	{
		AnimationChannel channel;
		channel.Path = path;
		channel.Interpolation = interpolation;
		channel.Times = std::move(times);
		channel.Values = std::move(values);
		return channel;
	}

}

TEST_SUITE("Animation")
{
	TEST_CASE("glTF skins, rigid attachments and clips are imported")
	{
		Test::TemporaryProject project;
		const MeshData mesh = ImportTestMesh(project);

		REQUIRE(mesh.IsSkinned());
		// Skeleton: every scene node, parents before children.
		REQUIRE(mesh.Skeleton.Nodes.size() == 4);
		for (size_t i = 0; i < mesh.Skeleton.Nodes.size(); i++)
			CHECK(mesh.Skeleton.Nodes[i].Parent < static_cast<int32_t>(i));
		const int32_t upper = mesh.Skeleton.FindNode("Upper");
		REQUIRE(upper >= 0);
		CHECK(mesh.Skeleton.Nodes[static_cast<size_t>(upper)].Parent == mesh.Skeleton.FindNode("Root"));
		CHECK(Test::Near(mesh.Skeleton.Nodes[static_cast<size_t>(upper)].Rest.Translation, glm::vec3(0.0f, 1.0f, 0.0f)));
		CHECK(mesh.Skeleton.FindNode("Missing") == -1);

		// Palette: two skin joints plus one rigid entry for the hat.
		REQUIRE(mesh.Joints.size() == 3);
		CHECK(mesh.Joints[1].Node == static_cast<uint32_t>(upper));
		CHECK(mesh.Joints[0].HasBounds);
		CHECK(mesh.Vertices.size() == 12 + 3);
		CHECK(mesh.SkinVertices.size() == mesh.Vertices.size());
		for (const SkinVertex& skin : mesh.SkinVertices)
		{
			CHECK(skin.Weights.x + skin.Weights.y + skin.Weights.z + skin.Weights.w == doctest::Approx(1.0f));
			for (int k = 0; k < 4; k++)
				CHECK(skin.Joints[k] < mesh.Joints.size());
		}
		// The skinned node's own transform (5, 5, 5) is ignored; vertices stay in bind space.
		CHECK(Test::Near(mesh.Vertices[0].Position, glm::vec3(-0.25f, 0.0f, -0.25f)));
		CHECK(mesh.Bounds.Max.y == doctest::Approx(2.3f)); // hat tip at 2 + 0.3

		// Submeshes remember their node, so parts can be hidden.
		REQUIRE(mesh.Submeshes.size() == 2);
		CHECK(mesh.Submeshes[0].Node == "Bar");
		CHECK(mesh.Submeshes[1].Node == "Hat");

		REQUIRE(mesh.Animations.size() == 3);
		const AnimationClip* bend = mesh.FindAnimation("Bend");
		REQUIRE(bend);
		CHECK(bend->Duration == doctest::Approx(1.0f));
		REQUIRE(bend->Channels.size() == 1);
		CHECK(bend->Channels[0].Path == AnimationPath::Rotation);
		CHECK(bend->Channels[0].Interpolation == AnimationInterpolation::Linear);
		CHECK(mesh.FindAnimation("Slide")->Channels[0].Interpolation == AnimationInterpolation::Step);
		CHECK(mesh.FindAnimation("Slide")->Duration == doctest::Approx(0.5f));
		const AnimationChannel& grow = mesh.FindAnimation("Grow")->Channels[0];
		CHECK(grow.Interpolation == AnimationInterpolation::CubicSpline);
		CHECK(grow.Values.size() == grow.Times.size() * 3);
		CHECK(mesh.FindAnimation("Nope") == nullptr);
	}

	TEST_CASE("Broken files are errors and static meshes stay unskinned")
	{
		Test::TemporaryProject project;
		project.WriteAsset("Models/Bad.gltf", "{ not json");
		std::string error;
		CHECK_FALSE(GltfImporter::Import(project.GetAssetDirectory() / "Models/Bad.gltf", error));
		CHECK_FALSE(error.empty());

		const std::shared_ptr<const MeshData> cube = AssetManager::GetMesh("builtin:cube");
		REQUIRE(cube);
		CHECK_FALSE(cube->IsSkinned());
		CHECK(cube->Animations.empty());
	}

	TEST_CASE("Skinning follows the joints: rest pose, bent joint, rigid attachment")
	{
		Test::TemporaryProject project;
		const MeshData mesh = ImportTestMesh(project);

		// Rest pose: the palette is identity for the skin and the hat stays where it was baked.
		std::vector<glm::mat4> rest;
		AnimationSystem::EvaluateSkinMatrices(mesh, nullptr, rest);
		for (size_t v = 0; v < mesh.Vertices.size(); v++)
			CHECK(Test::Near(SkinnedPosition(mesh, v, rest), mesh.Vertices[v].Position));

		// Bent 90 degrees about +Z at the Upper joint (y = 1): the top ring swings to -x.
		const std::vector<glm::mat4> bent = PoseAt(mesh, "Bend", 1.0f);
		const glm::vec3 top = SkinnedPosition(mesh, 8, bent); // ring 2, corner (-0.25, -0.25)
		CHECK(Test::Near(top, glm::vec3(-1.0f, 0.75f, -0.25f)));
		// Bottom ring is bound to Root only and does not move.
		CHECK(Test::Near(SkinnedPosition(mesh, 0, bent), mesh.Vertices[0].Position));
		// The hat (rigid, child of Upper at local y = 1) now points along -x.
		const glm::vec3 hatTip = SkinnedPosition(mesh, 14, bent);
		CHECK(Test::Near(hatTip, glm::vec3(-1.3f, 1.0f, 0.0f)));
		// Normals rotate with the joint and stay unit length.
		const Vertex skinned = Animation::SkinVertexCPU(mesh.Vertices[8], mesh.SkinVertices[8], bent);
		CHECK(glm::length(skinned.Normal) == doctest::Approx(1.0f));

		// Bounds follow the pose.
		const BoundingBox bounds = Animation::ComputeSkinnedBounds(mesh, bent);
		CHECK(bounds.Min.x <= -1.29f);
		CHECK(bounds.Max.y < 2.0f);
	}

	TEST_CASE("Keyframe interpolation: step, linear, cubic spline, clamping")
	{
		const AnimationChannel step = MakeChannel(AnimationPath::Translation, AnimationInterpolation::Step, { 0.0f, 1.0f, 2.0f },
			{ glm::vec4(0.0f), glm::vec4(10.0f), glm::vec4(20.0f) });
		CHECK(Animation::SampleChannel(step, 0.99f).x == doctest::Approx(0.0f));
		CHECK(Animation::SampleChannel(step, 1.0f).x == doctest::Approx(10.0f));
		CHECK(Animation::SampleChannel(step, 1.5f).x == doctest::Approx(10.0f));

		const AnimationChannel linear = MakeChannel(AnimationPath::Translation, AnimationInterpolation::Linear, { 1.0f, 3.0f },
			{ glm::vec4(0.0f), glm::vec4(4.0f, 8.0f, 0.0f, 0.0f) });
		CHECK(Animation::SampleChannel(linear, 2.0f).x == doctest::Approx(2.0f));
		CHECK(Animation::SampleChannel(linear, 2.5f).y == doctest::Approx(6.0f));
		CHECK(Animation::SampleChannel(linear, -5.0f).x == doctest::Approx(0.0f)); // clamped before the first key
		CHECK(Animation::SampleChannel(linear, 50.0f).x == doctest::Approx(4.0f)); // and after the last

		// Hermite spline with non-zero tangents, checked against the closed-form basis functions.
		const AnimationChannel cubic = MakeChannel(AnimationPath::Translation, AnimationInterpolation::CubicSpline, { 0.0f, 2.0f },
			{ glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(1.0f), glm::vec4(1.0f), glm::vec4(4.0f), glm::vec4(0.0f) });
		// p0 = 0, m0 = out-tangent 1 * 2 s, p1 = 4, m1 = in-tangent 1 * 2 s, at t = 0.5:
		const float t = 0.5f, h10 = t * t * t - 2 * t * t + t, h01 = -2 * t * t * t + 3 * t * t, h11 = t * t * t - t * t;
		CHECK(Animation::SampleChannel(cubic, 1.0f).x == doctest::Approx(h10 * 2.0f + h01 * 4.0f + h11 * 2.0f));
		CHECK(Animation::SampleChannel(cubic, 2.0f).x == doctest::Approx(4.0f));

		// Rotations take the shortest path and stay normalized.
		const glm::quat a = glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
		const glm::quat b = -glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)); // same rotation, opposite hemisphere
		const AnimationChannel rotation = MakeChannel(AnimationPath::Rotation, AnimationInterpolation::Linear, { 0.0f, 1.0f },
			{ glm::vec4(a.x, a.y, a.z, a.w), glm::vec4(b.x, b.y, b.z, b.w) });
		const glm::vec4 middle = Animation::SampleChannel(rotation, 0.5f);
		CHECK(glm::length(middle) == doctest::Approx(1.0f));
		const glm::quat mid(middle.w, middle.x, middle.y, middle.z);
		CHECK(glm::degrees(glm::angle(mid)) == doctest::Approx(20.0f).epsilon(0.01));

		// Empty channels return the identity for their path.
		const AnimationChannel empty = MakeChannel(AnimationPath::Scale, AnimationInterpolation::Linear, {}, {});
		CHECK(Test::Near(glm::vec3(Animation::SampleChannel(empty, 1.0f)), glm::vec3(1.0f)));
	}

	TEST_CASE("Imported clips sample with their interpolation")
	{
		Test::TemporaryProject project;
		const MeshData mesh = ImportTestMesh(project);
		const size_t root = static_cast<size_t>(mesh.Skeleton.FindNode("Root"));

		Pose pose = Animation::RestPose(mesh.Skeleton);
		Animation::SampleClip(*mesh.FindAnimation("Slide"), 0.49f, pose);
		CHECK(pose[root].Translation.x == doctest::Approx(0.0f));
		Animation::SampleClip(*mesh.FindAnimation("Slide"), 0.5f, pose);
		CHECK(pose[root].Translation.x == doctest::Approx(1.0f));

		Animation::SampleClip(*mesh.FindAnimation("Grow"), 0.5f, pose);
		CHECK(pose[root].Scale.y == doctest::Approx(1.5f)); // flat tangents: smoothstep halfway
	}

	TEST_CASE("Time wrapping and pose blending")
	{
		CHECK(Animation::WrapTime(2.5f, 1.0f, true) == doctest::Approx(0.5f));
		CHECK(Animation::WrapTime(-0.25f, 1.0f, true) == doctest::Approx(0.75f));
		CHECK(Animation::WrapTime(2.5f, 1.0f, false) == doctest::Approx(1.0f));
		CHECK(Animation::WrapTime(1.0f, 0.0f, true) == 0.0f);

		Pose a(1), b(1), out;
		a[0].Translation = glm::vec3(0.0f);
		b[0].Translation = glm::vec3(2.0f, 0.0f, 0.0f);
		a[0].Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		b[0].Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
		b[0].Scale = glm::vec3(3.0f);
		Animation::BlendPoses(a, b, 0.25f, out);
		REQUIRE(out.size() == 1);
		CHECK(out[0].Translation.x == doctest::Approx(0.5f));
		CHECK(out[0].Scale.x == doctest::Approx(1.5f));
		CHECK(glm::length(glm::vec4(out[0].Rotation.x, out[0].Rotation.y, out[0].Rotation.z, out[0].Rotation.w)) == doctest::Approx(1.0f));
		Animation::BlendPoses(a, b, 1.0f, a); // output may alias an input
		CHECK(a[0].Translation.x == doctest::Approx(2.0f));
	}

	TEST_CASE("Animator: looping, one-shots, crossfades and errors")
	{
		Test::TemporaryProject project;
		const MeshData mesh = ImportTestMesh(project);

		AnimatorComponent animator;
		animator.Clip = "Bend";
		animator.BlendDuration = 0.5f;
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.1f); // starts the clip (no fade from nothing)
		CHECK(animator.RuntimeClip == "Bend");
		CHECK(animator.RuntimeBlend == 1.0f);
		CHECK(AnimationSystem::IsPlaying(animator));
		const std::string bend = "Bend", slide = "Slide";
		CHECK(AnimationSystem::IsPlaying(animator, &bend));
		CHECK_FALSE(AnimationSystem::IsPlaying(animator, &slide));

		AnimationSystem::UpdateAnimator(animator, &mesh, 0.75f);
		CHECK(animator.RuntimeTime == doctest::Approx(0.75f));
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.5f); // loops
		CHECK(animator.RuntimeTime == doctest::Approx(0.25f));

		// Crossfade into a one-shot.
		std::string error;
		REQUIRE(AnimationSystem::Play(animator, &mesh, "Slide", 0.4f, false, error));
		CHECK_FALSE(animator.Loop);
		CHECK(AnimationSystem::IsPlaying(animator, &slide)); // requested, before the next update
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.1f);
		CHECK(animator.RuntimeClip == "Slide");
		CHECK(animator.RuntimePreviousClip == "Bend");
		CHECK(animator.RuntimeBlend == doctest::Approx(0.0f));
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.2f);
		CHECK(animator.RuntimeBlend == doctest::Approx(0.5f));
		// Mid-fade poses blend both clips: the hat is somewhere between upright and bent.
		std::vector<glm::mat4> skin;
		AnimationSystem::EvaluateSkinMatrices(mesh, &animator, skin);
		const glm::vec3 hat = Animation::SkinVertexCPU(mesh.Vertices[14], mesh.SkinVertices[14], skin).Position;
		CHECK(hat.y < 2.29f);
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.35f);
		CHECK(animator.RuntimeBlend == 1.0f);
		CHECK(animator.RuntimePreviousClip.empty());
		CHECK(animator.RuntimeFinished); // 0.5 s one-shot reached its end
		CHECK_FALSE(AnimationSystem::IsPlaying(animator));
		CHECK(animator.RuntimeTime == doctest::Approx(0.5f));

		// Replaying a finished one-shot restarts it.
		REQUIRE(AnimationSystem::Play(animator, &mesh, "Slide", -1.0f, std::nullopt, error));
		CHECK_FALSE(animator.RuntimeFinished);
		CHECK(animator.RuntimeTime == 0.0f);

		// Unknown clips are errors, not crashes.
		CHECK_FALSE(AnimationSystem::Play(animator, &mesh, "Dance", -1.0f, std::nullopt, error));
		CHECK(error.find("Dance") != std::string::npos);

		// Paused animators keep their frame; negative speed plays backwards.
		animator.Playing = false;
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.2f);
		CHECK(animator.RuntimeTime == 0.0f);
		animator.Playing = true;
		animator.Loop = true;
		animator.Speed = -1.0f;
		AnimationSystem::UpdateAnimator(animator, &mesh, 0.1f);
		CHECK(animator.RuntimeTime == doctest::Approx(0.4f));
	}

	TEST_CASE("Animator component: registry, scene update and Lua API")
	{
		Test::TemporaryProject project;
		project.WriteAsset("Models/Bar.gltf", Test::MakeSkinnedGltf());
		AssetManager::Clear();

		const ComponentInfo* info = ComponentRegistry::Find("Animator");
		REQUIRE(info);
		CHECK(info->Defaults().contains("BlendDuration"));
		CHECK_FALSE(info->Defaults().contains("Time")); // runtime fields are not saved

		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		Entity bar = scene.CreateEntity("Bar");
		bar.AddComponent<MeshComponent>().Mesh = "Models/Bar.gltf";
		bar.AddComponent<AnimatorComponent>().Clip = "Bend";
		std::string error;
		REQUIRE(info->Deserialize(bar, nlohmann::json{ { "Speed", 2.0 } }, error));

		scene.OnRuntimeStart();
		Test::Simulate(scene, 0.25f);
		const AnimatorComponent& animator = bar.GetComponent<AnimatorComponent>();
		CHECK(animator.RuntimeClip == "Bend");
		CHECK(animator.RuntimeTime > 0.3f); // speed 2

		std::string output;
		ScriptEngine& lua = *scene.GetScriptEngine();
		const bool ok = lua.Execute(R"(
			local bar = scene.find("Bar")
			local clips = anim.clips(bar)
			assert(#clips == 3 and clips[1] == "Bend", "clips")
			assert(anim.is_playing(bar) and anim.is_playing(bar, "Bend") and not anim.is_playing(bar, "Grow"), "is_playing")
			anim.play(bar, "Grow", 0.1, false)
			assert(anim.is_playing(bar, "Grow"), "play")
			anim.set_speed(bar, 0.5)
			assert(bar:get("Animator").Speed == 0.5, "set_speed")
			assert(not pcall(anim.play, bar, "Missing"), "unknown clip errors")
			assert(not pcall(anim.clips, scene.create("Plain")), "entities without an Animator error")
		)", output);
		CHECK_MESSAGE(ok, output);
		Test::Simulate(scene, 0.1f);
		CHECK(animator.RuntimeClip == "Grow");
		CHECK_FALSE(animator.Loop);
		scene.OnRuntimeStop();
		AssetManager::Clear();
	}
}
