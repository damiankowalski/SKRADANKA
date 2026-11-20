#include "TestHelpers.h"

#include "Penumbra/AI/FogOfWarSystem.h"
#include "Penumbra/AI/LuminanceProvider.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Asset/Importers.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Renderer/VulkanDeviceSelection.h"
#include "Penumbra/UI/Overlay.h"

#include <stb_image_write.h>

using namespace Penumbra;

// GPU tests run on WARP (the D3D12 software rasterizer) so they are deterministic and run on
// CI machines without a GPU. The NVRHI validation layer is enabled and must report no errors.
// They use the default graphics API: Vulkan on macOS, and on Windows too with PENUMBRA_GRAPHICS_API=vulkan.

namespace {

	struct RendererFixture
	{
		Test::TemporaryProject Project;
		Scope<RenderDevice> Device;
		Scope<SceneRenderer> Renderer;
		Scene SceneInstance;

		static constexpr uint32_t Width = 320;
		static constexpr uint32_t Height = 180;

		// Without `required`, a missing device leaves Device empty instead of failing the test.
		explicit RendererFixture(GraphicsApi api = GetDefaultGraphicsApi(), bool required = true)
		{
			RenderDeviceSpecification spec;
			spec.Api = api;
			spec.UseWarp = true;
			spec.EnableDebugLayer = true;
			Device = RenderDevice::Create(spec);
			if (!required && !Device)
				return;
			REQUIRE(Device);

			SceneRendererSettings settings;
			settings.CascadeResolution = 512;
			settings.SpotShadowResolution = 256;
			settings.PointShadowResolution = 128;
			Renderer = CreateScope<SceneRenderer>(*Device, settings);
			Renderer->SetViewportSize(Width, Height);
			SceneInstance.SetAudioDeviceEnabled(false);
		}

		std::vector<uint8_t> RenderFrame(const RenderCamera& camera)
		{
			REQUIRE(Device->BeginFrame());
			Renderer->Render(SceneInstance, camera);
			Device->EndFrame();
			std::vector<uint8_t> pixels;
			uint32_t width = 0, height = 0;
			REQUIRE(Renderer->ReadOutputPixels(pixels, width, height));
			REQUIRE(width == Width);
			REQUIRE(height == Height);
			return pixels;
		}

		static glm::vec3 Pixel(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y)
		{
			const size_t index = (static_cast<size_t>(y) * Width + x) * 4;
			return glm::vec3(pixels[index], pixels[index + 1], pixels[index + 2]) / 255.0f;
		}

		static glm::uvec2 ToPixel(const RenderCamera& camera, const glm::vec3& world)
		{
			const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(world, 1.0f);
			const glm::vec2 ndc = glm::vec2(clip) / clip.w;
			const glm::vec2 pixel((ndc.x * 0.5f + 0.5f) * Width, (0.5f - ndc.y * 0.5f) * Height);
			return glm::uvec2(glm::clamp(pixel, glm::vec2(0.0f), glm::vec2(Width - 1, Height - 1)));
		}

		RenderCamera Camera() const
		{
			return RenderCamera::Perspective({ 0.0f, 4.0f, 8.0f }, Math::LookRotation(glm::vec3(0.0f, -4.0f, -8.0f)), 60.0f,
				static_cast<float>(Width) / static_cast<float>(Height), 0.1f, 100.0f);
		}
	};

	// Writes a 16x8 equirectangular HDR: bright blue sky above the horizon, dark ground below.
	void WriteSkyHDR(const std::filesystem::path& path)
	{
		std::vector<float> pixels(16 * 8 * 3);
		for (int y = 0; y < 8; y++)
		{
			for (int x = 0; x < 16; x++)
			{
				float* p = &pixels[(static_cast<size_t>(y) * 16 + x) * 3];
				const bool sky = y < 4;
				p[0] = sky ? 0.3f : 0.05f;
				p[1] = sky ? 0.5f : 0.04f;
				p[2] = sky ? 1.2f : 0.03f;
			}
		}
		std::filesystem::create_directories(path.parent_path());
		REQUIRE(stbi_write_hdr(path.string().c_str(), 16, 8, 3, pixels.data()) != 0);
	}

}

TEST_SUITE("Renderer")
{
	TEST_CASE("A lit scene renders correctly on WARP without validation errors")
	{
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		WriteSkyHDR(fixture.Project.GetAssetDirectory() / "Environment" / "Sky.hdr");

		Entity environment = scene.CreateEntity("Environment");
		auto& env = environment.AddComponent<EnvironmentComponent>();
		env.EnvironmentMap = "Environment/Sky.hdr";
		env.EnvironmentIntensity = 0.3f;

		Entity sun = scene.CreateEntity("Sun");
		// Low sun behind the cube (as seen from the camera): its shadow falls towards the viewer.
		sun.Transform().Rotation = Math::LookRotation(glm::vec3(0.3f, -0.6f, 1.0f));
		auto& sunLight = sun.AddComponent<DirectionalLightComponent>();
		sunLight.Intensity = 2.0f;

		Test::CreateBox(scene, "Ground", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f }, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
		Test::CreateBox(scene, "RedCube", { 0.0f, 0.75f, 0.0f }, { 0.75f, 0.75f, 0.75f }, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

		Entity spot = scene.CreateEntity("Spot");
		spot.Transform().Translation = { 3.0f, 3.0f, 0.0f };
		spot.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, -1.0f, 0.0f));
		spot.AddComponent<SpotLightComponent>();
		Entity torch = scene.CreateEntity("Torch");
		torch.Transform().Translation = { -3.0f, 1.0f, 0.0f };
		auto& point = torch.AddComponent<PointLightComponent>();
		point.CastShadows = true;
		point.Flicker = 0.2f;

		Entity glass = Test::CreateBox(scene, "Glass", { 2.0f, 0.5f, 2.0f }, glm::vec3(0.5f), glm::vec4(0.2f, 0.8f, 1.0f, 0.4f));
		glass.GetComponent<MaterialComponent>().Translucent = true;
		Entity smoke = scene.CreateEntity("Smoke");
		smoke.Transform().Translation = { -2.0f, 1.0f, 2.0f };
		smoke.AddComponent<SmokeVolumeComponent>().Radius = 1.0f;

		scene.GetOverlay().AddRect({ { 0.0f, 0.0f }, { 20.0f, 20.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
		scene.GetOverlay().AddText({ { 30.0f, 4.0f }, "HUD", 2.0f, { 1.0f, 1.0f, 1.0f, 1.0f } });
		fixture.Renderer->GetDebugDraw().Box(glm::vec3(0.0f, 0.75f, 0.0f), glm::vec3(0.8f), glm::quat(1, 0, 0, 0), { 1, 1, 0, 1 });

		fixture.RenderFrame(fixture.Camera()); // first frame builds the IBL maps
		const std::vector<uint8_t> pixels = fixture.RenderFrame(fixture.Camera()); // steady state (IBL built)

		const SceneRendererStatistics& stats = fixture.Renderer->GetStatistics();
		CHECK(stats.Lights == 3);
		CHECK(stats.ShadowViews >= 4 + 1 + 6); // cascades + spot + point cube faces
		CHECK(stats.DrawCalls > 0);
		CHECK(fixture.Device->GetErrorCount() == 0);

		// HUD rectangle is drawn verbatim in the top-left corner.
		CHECK(Test::Near(RendererFixture::Pixel(pixels, 5, 5), glm::vec3(0.0f, 1.0f, 0.0f), 0.02f));

		// The red cube is red.
		const RenderCamera camera = fixture.Camera();
		const glm::uvec2 cubePixel = RendererFixture::ToPixel(camera, { 0.0f, 1.5f, 0.0f }); // sunlit top face
		const glm::vec3 cube = RendererFixture::Pixel(pixels, cubePixel.x, cubePixel.y);
		CHECK(cube.r > cube.g * 2.0f);
		CHECK(cube.r > 0.2f);

		// The cube's shadow darkens the ground in front of it compared with open ground beside it.
		const glm::uvec2 shadowPixel = RendererFixture::ToPixel(camera, { 0.5f, 0.0f, 1.6f });
		const glm::uvec2 litPixel = RendererFixture::ToPixel(camera, { -3.0f, 0.0f, 1.6f });
		const float shadowed = Math::Luminance(RendererFixture::Pixel(pixels, shadowPixel.x, shadowPixel.y));
		const float lit = Math::Luminance(RendererFixture::Pixel(pixels, litPixel.x, litPixel.y));
		CHECK(shadowed < lit * 0.8f);

		// The sky at the top of the image shows the blue environment.
		const glm::vec3 sky = RendererFixture::Pixel(pixels, RendererFixture::Width - 10, 3);
		CHECK(sky.b > sky.r);

		// PENUMBRA_TEST_OUTPUT=<dir> keeps the rendered image for inspection.
		std::filesystem::path screenshot = fixture.Project.GetDirectory() / "screenshot.png";
		if (const char* outputDirectory = std::getenv("PENUMBRA_TEST_OUTPUT"))
			screenshot = std::filesystem::path(outputDirectory) / "RendererLitScene.png";
		CHECK(fixture.Renderer->SaveScreenshot(screenshot));
		CHECK(std::filesystem::file_size(screenshot) > 100);
	}

	TEST_CASE("Skinned meshes are posed on the GPU and parts can be hidden")
	{
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		fixture.Project.WriteAsset("Models/Bar.gltf", Test::MakeSkinnedGltf());
		AssetManager::Clear();

		Entity bar = scene.CreateEntity("Bar");
		bar.AddComponent<MeshComponent>().Mesh = "Models/Bar.gltf";
		auto& material = bar.AddComponent<MaterialComponent>();
		material.BaseColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		material.EmissiveColor = glm::vec3(1.0f, 0.0f, 0.0f);
		material.EmissiveIntensity = 2.0f;
		auto& animator = bar.AddComponent<AnimatorComponent>();

		const RenderCamera camera = fixture.Camera();
		auto isRed = [&](const std::vector<uint8_t>& pixels, const glm::vec3& world) {
			const glm::uvec2 pixel = RendererFixture::ToPixel(camera, world);
			const glm::vec3 color = RendererFixture::Pixel(pixels, pixel.x, pixel.y);
			return color.r > 0.3f && color.r > color.g * 2.0f;
		};

		// No clip: the bind pose, standing upright.
		std::vector<uint8_t> pixels = fixture.RenderFrame(camera);
		CHECK(isRed(pixels, { 0.0f, 1.75f, 0.0f }));
		CHECK_FALSE(isRed(pixels, { -0.8f, 1.0f, 0.0f }));
		const uint32_t drawsWithHat = fixture.Renderer->GetStatistics().DrawCalls;

		// "Bend" at its end: the upper half lies along -x. The skinned vertex buffer feeds the
		// prepass, forward and shadow passes alike.
		animator.Clip = "Bend";
		animator.RuntimeClip = "Bend";
		animator.RuntimeTime = 1.0f;
		pixels = fixture.RenderFrame(camera);
		CHECK_FALSE(isRed(pixels, { 0.0f, 1.75f, 0.0f }));
		CHECK(isRed(pixels, { -0.8f, 1.0f, 0.0f }));
		CHECK(isRed(pixels, { 0.0f, 0.3f, 0.0f })); // the lower half stays put

		// Hidden nodes are not drawn at all.
		bar.GetComponent<MeshComponent>().HiddenNodes = { "Hat" };
		fixture.RenderFrame(camera);
		CHECK(fixture.Renderer->GetStatistics().DrawCalls < drawsWithHat);
		CHECK(fixture.Device->GetErrorCount() == 0);

		if (const char* outputDirectory = std::getenv("PENUMBRA_TEST_OUTPUT"))
			CHECK(fixture.Renderer->SaveScreenshot(std::filesystem::path(outputDirectory) / "RendererSkinned.png"));
		AssetManager::Clear();
	}

	TEST_CASE("Rendering without environment, lights or meshes is valid")
	{
		RendererFixture fixture;
		const std::vector<uint8_t> pixels = fixture.RenderFrame(fixture.Camera());
		CHECK(fixture.Device->GetErrorCount() == 0);
		CHECK(pixels.size() == static_cast<size_t>(RendererFixture::Width) * RendererFixture::Height * 4);

		fixture.Renderer->SetViewportSize(64, 64);
		CHECK(fixture.Renderer->GetViewportWidth() == 64);
	}

	TEST_CASE("The GPU time of a frame and the video memory in use are measured for the performance overlay")
	{
		RendererFixture fixture;
		CHECK_FALSE(fixture.Renderer->GetGpuTimeMs().has_value()); // nothing rendered yet
		// Results arrive frames later; a few frames are always enough.
		for (int frame = 0; frame < 8; ++frame)
			fixture.RenderFrame(fixture.Camera());
		fixture.Device->WaitForIdle();
		fixture.RenderFrame(fixture.Camera());
		REQUIRE(fixture.Renderer->GetGpuTimeMs().has_value());
		CHECK(*fixture.Renderer->GetGpuTimeMs() >= 0.0f);
		CHECK(*fixture.Renderer->GetGpuTimeMs() < 10000.0f);
		CHECK(fixture.Device->GetErrorCount() == 0);

		VideoMemoryInfo memory;
		if (fixture.Device->QueryVideoMemory(memory)) // the driver may not tell
		{
			CHECK(memory.BudgetBytes > 0);
			if (memory.HasUsage)
				CHECK(memory.UsedBytes <= memory.BudgetBytes * 2);
		}
	}

	TEST_CASE("Frustum culling")
	{
		const RenderCamera camera = RenderCamera::Perspective(glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 60.0f, 1.0f, 0.1f, 100.0f);
		const Frustum frustum(camera.Projection * camera.View);
		CHECK(frustum.IntersectsSphere({ 0.0f, 0.0f, -10.0f }, 1.0f));
		CHECK_FALSE(frustum.IntersectsSphere({ 0.0f, 0.0f, 10.0f }, 1.0f));
		CHECK_FALSE(frustum.IntersectsSphere({ 0.0f, 0.0f, -200.0f }, 1.0f));
		BoundingBox box;
		box.Expand(glm::vec3(-0.5f));
		box.Expand(glm::vec3(0.5f));
		CHECK(frustum.IntersectsBox(box, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f))));
		CHECK_FALSE(frustum.IntersectsBox(box, glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 0.0f, -5.0f))));
	}
}

TEST_SUITE("Renderer")
{
	TEST_CASE("Direct3D 12 and Vulkan render the same picture")
	{
		// Needs both APIs: Windows with a Vulkan driver. CI's GPU-less Windows has no Vulkan device, and
		// macOS has no D3D12; there the other renderer tests cover the one API that exists.
		if (!RenderDevice::IsGraphicsApiAvailable(GraphicsApi::D3D12))
			return;
		RendererFixture vulkan(GraphicsApi::Vulkan, false);
		if (!vulkan.Device)
		{
			MESSAGE("No Vulkan device: D3D12/Vulkan comparison skipped");
			return;
		}
		RendererFixture d3d12(GraphicsApi::D3D12);

		auto build = [](RendererFixture& fixture) {
			Scene& scene = fixture.SceneInstance;
			WriteSkyHDR(fixture.Project.GetAssetDirectory() / "Environment" / "Sky.hdr");
			Entity environment = scene.CreateEntity("Environment");
			auto& env = environment.AddComponent<EnvironmentComponent>();
			env.EnvironmentMap = "Environment/Sky.hdr";
			env.EnvironmentIntensity = 0.3f;
			Entity sun = scene.CreateEntity("Sun");
			sun.Transform().Rotation = Math::LookRotation(glm::vec3(0.3f, -0.6f, 1.0f));
			sun.AddComponent<DirectionalLightComponent>().Intensity = 2.0f;
			Test::CreateBox(scene, "Ground", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f }, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
			Test::CreateBox(scene, "RedCube", { 0.0f, 0.75f, 0.0f }, { 0.75f, 0.75f, 0.75f }, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
			Entity spot = scene.CreateEntity("Spot");
			spot.Transform().Translation = { 3.0f, 3.0f, 0.0f };
			spot.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, -1.0f, 0.0f));
			spot.AddComponent<SpotLightComponent>();
			scene.GetOverlay().AddRect({ { 0.0f, 0.0f }, { 20.0f, 20.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
			fixture.RenderFrame(fixture.Camera()); // builds the IBL maps
			return fixture.RenderFrame(fixture.Camera());
		};
		const std::vector<uint8_t> a = build(d3d12);
		const std::vector<uint8_t> b = build(vulkan);
		REQUIRE(a.size() == b.size());

		// Same orientation, colours and lighting: per-pixel differences stay small on average (WARP and a
		// GPU round and filter shadows a little differently), and nearly no pixel is far off.
		double total = 0.0;
		size_t farOff = 0;
		for (size_t i = 0; i < a.size(); i += 4)
		{
			int worst = 0;
			for (size_t c = 0; c < 3; c++)
			{
				const int difference = std::abs(static_cast<int>(a[i + c]) - static_cast<int>(b[i + c]));
				total += difference;
				worst = std::max(worst, difference);
			}
			if (worst > 48)
				farOff++;
		}
		const size_t pixelCount = a.size() / 4;
		const double meanDifference = total / (pixelCount * 3.0) / 255.0;
		CHECK(meanDifference < 0.02);
		CHECK(farOff < pixelCount / 100);
		CHECK(RendererFixture::Pixel(b, 5, 5).g > 0.95f); // the HUD in the top-left corner: not upside down
		CHECK(vulkan.Device->GetErrorCount() == 0);
		CHECK(d3d12.Device->GetErrorCount() == 0);
		if (const char* outputDirectory = std::getenv("PENUMBRA_TEST_OUTPUT"))
			CHECK(vulkan.Renderer->SaveScreenshot(std::filesystem::path(outputDirectory) / "RendererVulkan.png"));
	}

	TEST_CASE("Flames render as warm fire and disappear when disabled")
	{
		RendererFixture fixture;
		Entity fire = fixture.SceneInstance.CreateEntity("Fire");
		auto& flame = fire.AddComponent<FlameComponent>();
		flame.Height = 2.0f;
		flame.Radius = 0.6f;
		const RenderCamera camera = RenderCamera::Perspective({ 0.0f, 1.0f, 3.0f }, glm::quat(1, 0, 0, 0), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 50.0f);

		auto brightestWarmth = [&]() {
			const std::vector<uint8_t> pixels = fixture.RenderFrame(camera);
			float best = 0.0f;
			for (uint32_t y = 0; y < RendererFixture::Height; y += 2)
				for (uint32_t x = 0; x < RendererFixture::Width; x += 2)
				{
					const glm::vec3 c = RendererFixture::Pixel(pixels, x, y);
					best = std::max(best, c.r - c.b);
				}
			return best;
		};
		CHECK(brightestWarmth() > 0.3f); // orange/red fire on a black background
		flame.Enabled = false;
		CHECK(brightestWarmth() < 0.05f);
		CHECK(fixture.Device->GetErrorCount() == 0);
	}

	TEST_CASE("A material's UVScale repeats its texture and UVOffset shifts it")
	{
		// A texture dark on its left half and bright on its right, on a flat box facing the camera. Brightness
		// is read at the middle of each quarter of the face: once over it the halves cover two quarters each,
		// twice over they alternate, and shifted by half a repeat they swap.
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		std::vector<uint8_t> texels(64 * 8 * 4, 255);
		for (uint32_t y = 0; y < 8; y++)
			for (uint32_t x = 0; x < 32; x++)
				for (uint32_t c = 0; c < 3; c++)
					texels[(y * 64 + x) * 4 + c] = 0;
		REQUIRE(TextureImporter::WritePNG(fixture.Project.GetAssetDirectory() / "Textures" / "Halves.png", 64, 8, texels.data(), 64 * 4));

		auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
		environment.AmbientColor = glm::vec3(0.6f);
		environment.SSAOEnabled = false;
		environment.BloomIntensity = 0.0f;
		Entity sun = scene.CreateEntity("Sun");
		sun.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, 0.0f, -1.0f));
		sun.AddComponent<DirectionalLightComponent>().Intensity = 2.0f;
		Entity panel = Test::CreateBox(scene, "Panel", { 0.0f, 0.0f, 0.0f }, { 2.0f, 1.0f, 0.05f }, glm::vec4(1.0f));
		auto& material = panel.GetComponent<MaterialComponent>();
		material.AlbedoTexture = "Textures/Halves.png";

		const RenderCamera camera = RenderCamera::Perspective({ 0.0f, 0.0f, 4.0f }, Math::LookRotation(glm::vec3(0.0f, 0.0f, -1.0f)), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 100.0f);
		auto quarters = [&]() {
			const std::vector<uint8_t> pixels = fixture.RenderFrame(camera);
			std::array<float, 4> brightness{};
			for (int i = 0; i < 4; i++)
			{
				const glm::uvec2 pixel = RendererFixture::ToPixel(camera, { -1.5f + static_cast<float>(i), 0.0f, 0.05f });
				brightness[static_cast<size_t>(i)] = Math::Luminance(RendererFixture::Pixel(pixels, pixel.x, pixel.y));
			}
			return brightness;
		};

		const std::array<float, 4> once = quarters();
		CHECK(std::abs(once[0] - once[1]) < 0.05f);
		CHECK(std::abs(once[2] - once[3]) < 0.05f);
		CHECK(std::abs(once[0] - once[2]) > 0.2f);

		material.UVScale = { 2.0f, 1.0f };
		const std::array<float, 4> twice = quarters();
		CHECK(std::abs(twice[0] - twice[2]) < 0.05f);
		CHECK(std::abs(twice[1] - twice[3]) < 0.05f);
		CHECK(std::abs(twice[0] - twice[1]) > 0.2f);

		material.UVScale = { 1.0f, 1.0f };
		material.UVOffset = { 0.5f, 0.0f };
		const std::array<float, 4> shifted = quarters();
		CHECK(shifted[0] == doctest::Approx(once[2]).epsilon(0.1));
		CHECK(shifted[3] == doctest::Approx(once[0]).epsilon(0.1));
		CHECK(fixture.Device->GetErrorCount() == 0);
	}

	TEST_CASE("World-space HUD faces the camera and is hidden by what stands in front of it")
	{
		RendererFixture fixture;
		Test::CreateBox(fixture.SceneInstance, "Wall", { 0.0f, 1.0f, 0.0f }, { 2.0f, 1.0f, 0.1f }, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
		// Seen at an angle: the quads still face the camera.
		const RenderCamera camera = RenderCamera::Perspective({ 1.5f, 1.2f, 4.0f }, Math::LookRotation(glm::vec3(-1.5f, -0.2f, -4.0f)), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 50.0f);
		const glm::vec3 inFront(-0.5f, 1.0f, 1.0f), behind(0.5f, 1.0f, -1.0f);
		OverlayList& overlay = fixture.SceneInstance.GetOverlay();
		auto draw = [&]() {
			overlay.Clear();
			overlay.AddWorldRect({ inFront, { 0.4f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
			overlay.AddWorldRect({ behind, { 0.4f, 0.2f }, { 0.0f, 1.0f, 0.0f, 1.0f } });
			overlay.AddWorldText({ inFront + glm::vec3(0.0f, -0.3f, 0.0f), "WASD", 0.15f, { 1.0f, 0.0f, 1.0f, 1.0f } });
			return fixture.RenderFrame(camera);
		};
		auto isGreen = [](const glm::vec3& c) { return c.g > 0.8f && c.r < 0.2f && c.b < 0.2f; };

		const std::vector<uint8_t> pixels = draw();
		const glm::uvec2 front = RendererFixture::ToPixel(camera, inFront);
		const glm::uvec2 hidden = RendererFixture::ToPixel(camera, behind);
		CHECK(isGreen(RendererFixture::Pixel(pixels, front.x, front.y)));
		CHECK_FALSE(isGreen(RendererFixture::Pixel(pixels, hidden.x, hidden.y)));
		// The letters are drawn too (magenta somewhere under the bar).
		int magenta = 0;
		for (uint32_t y = 0; y < RendererFixture::Height; y++)
			for (uint32_t x = 0; x < RendererFixture::Width; x++)
			{
				const glm::vec3 c = RendererFixture::Pixel(pixels, x, y);
				magenta += c.r > 0.8f && c.b > 0.8f && c.g < 0.2f ? 1 : 0;
			}
		CHECK(magenta > 10);

		// Without the wall the far one shows as well.
		fixture.SceneInstance.DestroyEntity(fixture.SceneInstance.FindEntityByName("Wall"));
		const std::vector<uint8_t> open = draw();
		CHECK(isGreen(RendererFixture::Pixel(open, hidden.x, hidden.y)));
		CHECK(fixture.Device->GetErrorCount() == 0);
	}

	TEST_CASE("Volumetric fog glows round a lamp, and a lamp's shadow keeps the fog dark behind a wall")
	{
		// A dark, foggy void: a lamp ahead of the camera, a wall to its right (edge-on to the camera) that
		// shades the air beyond it. Nothing else is lit, so what shows in the air is the fog's own light.
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
		environment.AmbientColor = glm::vec3(0.0f);
		environment.FogColor = glm::vec3(0.0f);
		environment.FogDensity = 0.15f;
		environment.FogScattering = 0.8f;
		environment.FogAnisotropy = 0.0f;
		environment.SSAOEnabled = false;
		environment.BloomIntensity = 0.0f;
		Test::CreateBox(scene, "Wall", { 0.0f, 1.5f, -6.0f }, { 0.05f, 3.0f, 3.0f }, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		Entity lamp = scene.CreateEntity("Lamp");
		lamp.Transform().Translation = { -1.0f, 1.5f, -6.0f };
		auto& light = lamp.AddComponent<PointLightComponent>();
		light.Intensity = 20.0f;
		light.Range = 8.0f;
		light.CastShadows = true;

		const glm::vec3 eye(0.0f, 1.5f, 4.0f);
		const RenderCamera camera = RenderCamera::Perspective(eye, Math::LookRotation(glm::vec3(0.0f, 0.0f, -1.0f)), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 100.0f);
		// Air beside the lamp on its own side, and the same distance away behind the wall.
		const glm::uvec2 litAir = RendererFixture::ToPixel(camera, { -3.0f, 1.5f, -6.0f });
		const glm::uvec2 shadedAir = RendererFixture::ToPixel(camera, { 1.0f, 1.5f, -6.0f });
		auto brightness = [&](const std::vector<uint8_t>& pixels, const glm::uvec2& pixel) {
			return Math::Luminance(RendererFixture::Pixel(pixels, pixel.x, pixel.y));
		};

		const std::vector<uint8_t> foggy = fixture.RenderFrame(camera);
		CHECK(brightness(foggy, litAir) > 0.15f);                          // the air glows
		CHECK(brightness(foggy, shadedAir) < brightness(foggy, litAir) * 0.6f); // the wall's shadow in the fog

		// Without volumetric fog the same air stays black (the fog only hides things).
		environment.VolumetricFog = false;
		const std::vector<uint8_t> plain = fixture.RenderFrame(camera);
		CHECK(brightness(plain, litAir) < 0.02f);

		// A lamp without a shadow map lights the fog on both sides (half as much: it would shine through walls).
		environment.VolumetricFog = true;
		light.CastShadows = false;
		fixture.RenderFrame(camera);
		const std::vector<uint8_t> unshadowed = fixture.RenderFrame(camera);
		CHECK(brightness(unshadowed, shadedAir) > brightness(unshadowed, litAir) * 0.6f);
		CHECK(fixture.Device->GetErrorCount() == 0);
	}

	TEST_CASE("Guards' fog glow agrees with the renderer's: the CPU estimate along sight lines matches the froxels")
	{
		// Perception integrates the fog's glow on the CPU (FogMedium::Integrate with the lamps' FogLight); the
		// renderer in its froxel grid. Lit fog with a lamp shadowed by a wall, a lamp without a shadow map, a
		// bank of mist and the fog's own glow; sight lines at eye level through all of it, compared at several
		// depths. The first frame of a scene has no temporal history (jitter 0.5): the grid is deterministic.
		// Tolerance (docs/Perception.md): glow within 20% or 0.002, transmittance within 0.03. Measured on an M1 Pro:
		// worst 16% on dim lines in the wall's shadow (the GPU's coarse one-tap shadow), 2-5% elsewhere.
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
		environment.AmbientColor = glm::vec3(0.0f);
		environment.FogColor = glm::vec3(0.01f);
		environment.FogDensity = 0.06f;
		environment.FogScattering = 0.5f;
		environment.FogAnisotropy = 0.3f;
		environment.SSAOEnabled = false;
		environment.BloomIntensity = 0.0f;
		Test::CreateBox(scene, "Wall", { 0.6f, 1.5f, -8.0f }, { 0.05f, 3.0f, 3.0f }, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		auto addLamp = [&](const glm::vec3& position, float intensity, float range, bool shadows) {
			Entity lamp = scene.CreateEntity("Lamp");
			lamp.Transform().Translation = position;
			auto& light = lamp.AddComponent<PointLightComponent>();
			light.Intensity = intensity;
			light.Range = range;
			light.Flicker = 0.0f;
			light.CastShadows = shadows;
		};
		addLamp({ -1.5f, 1.5f, -8.0f }, 20.0f, 8.0f, true);   // the wall at x = 0.6 shades the air beyond it
		addLamp({ 3.0f, 1.5f, -15.0f }, 15.0f, 6.0f, false);  // no shadow map: FogUnshadowedLight
		Entity mist = scene.CreateEntity("Mist");
		mist.Transform().Translation = { -2.0f, 1.5f, -20.0f };
		auto& bank = mist.AddComponent<FogVolumeComponent>();
		bank.HalfExtents = { 3.0f, 2.0f, 3.0f };
		bank.Density = 0.3f;
		bank.EdgeFade = 1.0f;
		scene.OnRuntimeStart();

		const glm::vec3 eye(0.0f, 1.5f, 4.0f);
		const RenderCamera camera = RenderCamera::Perspective(eye, Math::LookRotation(glm::vec3(0.0f, 0.0f, -1.0f)), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 100.0f);
		fixture.RenderFrame(camera);
		std::vector<glm::vec4> volume;
		glm::uvec3 size(0);
		REQUIRE(fixture.Renderer->ReadFogVolume(volume, size));
		REQUIRE(size == glm::uvec3(fixture.Renderer->GetSettings().FogGridWidth, fixture.Renderer->GetSettings().FogGridHeight,
							 fixture.Renderer->GetSettings().FogGridDepth));
		const float fogDistance = fixture.Renderer->GetSettings().FogDistance;

		AnalyticLuminanceProvider provider(scene);
		provider.BeginFrame(0.0f);
		const FogMedium medium = FogMedium::FromScene(scene);
		const glm::mat4 inverseViewProjection = glm::inverse(camera.Projection * camera.View);
		const glm::vec3 forward(0.0f, 0.0f, -1.0f);

		int compared = 0, glowing = 0;
		float worstGlow = 0.0f, worstTransmittance = 0.0f;
		for (const uint32_t column : { 40u, 60u, 72u, 88u, 100u, 120u })
		{
			for (const uint32_t slice : { 20u, 30u, 38u, 44u, 50u })
			{
				// The froxel column's ray, as shaders/Fog.hlsl builds it, out to the slice's far end.
				const uint32_t row = size.y / 2;
				const glm::vec2 uv = (glm::vec2(column, row) + 0.5f) / glm::vec2(size.x, size.y);
				const glm::vec4 farPlane = inverseViewProjection * glm::vec4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);
				const glm::vec3 direction = glm::normalize(glm::vec3(farPlane) / farPlane.w - eye);
				const float w = static_cast<float>(slice + 1) / static_cast<float>(size.z);
				const float distance = fogDistance * w * w / glm::dot(direction, forward);
				const FogPath cpu = medium.Integrate(eye, eye + direction * distance, [&](const glm::vec3& point) {
					return provider.FogLight(point, glm::normalize(eye - point), medium.Anisotropy);
				});
				const glm::vec4 gpu = volume[(static_cast<size_t>(slice) * size.y + row) * size.x + column];
				const float gpuGlow = Math::Luminance(glm::vec3(gpu));
				CAPTURE(column);
				CAPTURE(slice);
				CAPTURE(cpu.InScattered);
				CAPTURE(gpuGlow);
				CAPTURE(cpu.Transmittance);
				CAPTURE(gpu.a);
				CHECK(std::abs(cpu.Transmittance - gpu.a) <= 0.03f);
				CHECK(std::abs(cpu.InScattered - gpuGlow) <= std::max(0.2f * gpuGlow, 0.002f));
				compared++;
				glowing += gpuGlow > 0.05f ? 1 : 0;
				if (gpuGlow > 0.01f)
					worstGlow = std::max(worstGlow, std::abs(cpu.InScattered - gpuGlow) / gpuGlow);
				worstTransmittance = std::max(worstTransmittance, std::abs(cpu.Transmittance - gpu.a));
			}
		}
		MESSAGE("worst glow difference ", worstGlow * 100.0f, "% (where it glows), worst transmittance difference ", worstTransmittance);
		CHECK(compared == 30);
		CHECK(glowing >= 10); // most lines pass through lamp-lit fog: the comparison is about the glow
		scene.OnRuntimeStop();
		CHECK(fixture.Device->GetErrorCount() == 0);
	}

	TEST_CASE("A fog volume shows as a bank of haze lit by the lamp inside it; clear air around it stays black")
	{
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
		environment.AmbientColor = glm::vec3(0.0f);
		environment.FogColor = glm::vec3(0.0f);
		environment.SSAOEnabled = false;
		environment.BloomIntensity = 0.0f;
		Entity bank = scene.CreateEntity("Bank");
		bank.Transform().Translation = { -2.0f, 1.0f, -5.0f };
		auto& volume = bank.AddComponent<FogVolumeComponent>();
		volume.HalfExtents = { 1.5f, 1.5f, 1.5f };
		volume.Density = 0.5f;
		volume.EdgeFade = 0.2f;
		Entity lamp = scene.CreateEntity("Lamp");
		lamp.Transform().Translation = { 0.0f, 1.0f, -5.0f };
		auto& light = lamp.AddComponent<PointLightComponent>();
		light.Intensity = 10.0f;
		light.Range = 8.0f;

		const glm::vec3 eye(0.0f, 1.0f, 3.0f);
		const RenderCamera camera = RenderCamera::Perspective(eye, Math::LookRotation(glm::vec3(0.0f, 0.0f, -1.0f)), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 100.0f);
		const std::vector<uint8_t> pixels = fixture.RenderFrame(camera);
		const glm::uvec2 inBank = RendererFixture::ToPixel(camera, { -2.0f, 1.0f, -5.0f });
		const glm::uvec2 mirror = RendererFixture::ToPixel(camera, { 2.0f, 1.0f, -5.0f }); // as close to the lamp, no fog
		CHECK(Math::Luminance(RendererFixture::Pixel(pixels, inBank.x, inBank.y)) > 0.1f);
		CHECK(Math::Luminance(RendererFixture::Pixel(pixels, mirror.x, mirror.y)) < 0.02f);
		CHECK(fixture.Device->GetErrorCount() == 0);
	}

	TEST_CASE("Fog of war darkens and greys what the thief does not see, and leaves out guards there")
	{
		RendererFixture fixture;
		Scene& scene = fixture.SceneInstance;
		auto glowingBox = [&](const std::string& name, const glm::vec3& position, const glm::vec3& color) {
			Entity box = Test::CreateBox(scene, name, position, glm::vec3(0.5f), glm::vec4(color, 1.0f), 1.0f);
			box.GetComponent<MaterialComponent>().EmissiveColor = color;
			return box;
		};
		Test::CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
		Test::CreateBox(scene, "Wall", { 0.0f, 1.5f, -3.1f }, { 1.5f, 1.5f, 0.1f }, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
		glowingBox("Seen", { 1.0f, 0.5f, -1.5f }, { 0.0f, 1.0f, 0.0f });
		glowingBox("BehindWall", { -0.6f, 0.5f, -6.0f }, { 1.0f, 0.0f, 0.0f });
		// A guard behind the wall: his body is a child without a collider.
		Entity guard = scene.CreateEntity("Guard");
		guard.Transform().Translation = { 0.8f, 0.0f, -6.0f };
		guard.AddComponent<PerceptionComponent>();
		Entity body = glowingBox("GuardBody", { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f });
		body.RemoveComponent<BoxColliderComponent>();
		scene.SetParent(body, guard, false);
		// The lantern he carries: its light goes with him (a lamp on a wall would stay lit).
		Entity lantern = scene.CreateEntity("Lantern");
		lantern.AddComponent<PointLightComponent>().Range = 6.0f;
		scene.SetParent(lantern, guard, false);
		lantern.Transform().Translation = { 0.0f, 1.2f, 0.0f };

		Entity thief = scene.CreateEntity("Thief");
		thief.Transform().Translation = { 0.0f, 0.05f, 0.0f };
		thief.AddComponent<CharacterControllerComponent>();
		auto& fog = thief.AddComponent<FogOfWarComponent>();
		fog.Brightness = 0.2f;
		fog.Saturation = 0.0f;
		// High above and behind him, looking over the wall: his cone faces -Z like the camera.
		const glm::vec3 eye(0.0f, 9.0f, 4.0f), target(0.0f, 0.0f, -4.0f);
		Entity cameraEntity = scene.CreateEntity("Camera");
		cameraEntity.Transform().Translation = eye;
		cameraEntity.Transform().Rotation = Math::LookRotation(target - eye);
		cameraEntity.AddComponent<CameraComponent>();
		const RenderCamera camera = RenderCamera::Perspective(eye, Math::LookRotation(target - eye), 60.0f,
			static_cast<float>(RendererFixture::Width) / static_cast<float>(RendererFixture::Height), 0.1f, 100.0f);

		scene.OnRuntimeStart();
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		REQUIRE(scene.GetFogOfWarSystem()->GetView());

		const glm::uvec2 seen = RendererFixture::ToPixel(camera, { 1.0f, 1.0f, -1.5f });
		const glm::uvec2 behind = RendererFixture::ToPixel(camera, { -0.6f, 1.0f, -6.0f });
		const glm::uvec2 guardPixel = RendererFixture::ToPixel(camera, { 0.8f, 1.0f, -6.0f });

		const std::vector<uint8_t> fogged = fixture.RenderFrame(camera);
		const glm::vec3 seenColor = RendererFixture::Pixel(fogged, seen.x, seen.y);
		const glm::vec3 behindColor = RendererFixture::Pixel(fogged, behind.x, behind.y);
		CHECK(seenColor.g > 0.5f); // in view: as it is
		CHECK(seenColor.g > seenColor.r + 0.4f);
		CHECK(std::abs(behindColor.r - behindColor.g) < 0.05f); // behind the wall: grey
		CHECK(scene.GetFogOfWarSystem()->IsHidden(guard));
		const uint32_t lightsFogged = fixture.Renderer->GetStatistics().Lights;

		// The testing switch shows everything as it is.
		scene.GetFogOfWarSystem()->SetForcedOff(true);
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		const std::vector<uint8_t> clear = fixture.RenderFrame(camera);
		const glm::vec3 behindClear = RendererFixture::Pixel(clear, behind.x, behind.y);
		CHECK(behindClear.r > 0.5f);
		CHECK(behindClear.r > behindColor.r + 0.2f);
		CHECK(RendererFixture::Pixel(clear, guardPixel.x, guardPixel.y).b > 0.5f);
		CHECK(fixture.Renderer->GetStatistics().Lights == lightsFogged + 1); // his lantern shines again

		// Fog that changes nothing on screen still leaves the guard out: he is not drawn at all.
		scene.GetFogOfWarSystem()->SetForcedOff(false);
		fog.Brightness = 1.0f;
		fog.Saturation = 1.0f;
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		const std::vector<uint8_t> unseen = fixture.RenderFrame(camera);
		CHECK(RendererFixture::Pixel(unseen, behind.x, behind.y).r > 0.5f);
		CHECK(RendererFixture::Pixel(unseen, guardPixel.x, guardPixel.y).b < 0.2f);
		scene.OnRuntimeStop();
		CHECK(fixture.Device->GetErrorCount() == 0);
	}
}

TEST_SUITE("Renderer")
{
	TEST_CASE("Vulkan device choice prefers MoltenVK, can be forced to another driver and is otherwise stable")
	{
		// What vulkaninfo shows on an M1 Pro with the LunarG SDK: the same GPU through two drivers,
		// KosmicKrisp first.
		const std::vector<VulkanDeviceCandidate> mac = {
			{ "Apple M1 Pro", "KosmicKrisp", VulkanDeviceKind::Integrated, true },
			{ "Apple M1 Pro", "MoltenVK", VulkanDeviceKind::Integrated, true },
		};

		SUBCASE("MoltenVK wins a tie whatever its position")
		{
			const VulkanDeviceChoice choice = ChooseVulkanDevice(mac, "");
			REQUIRE(choice.Index);
			CHECK(mac[*choice.Index].DriverName == "MoltenVK");
			CHECK_FALSE(choice.Forced);
			CHECK_FALSE(choice.RequestIgnored);

			const std::vector<VulkanDeviceCandidate> reversed = { mac[1], mac[0] };
			CHECK(reversed[*ChooseVulkanDevice(reversed, "").Index].DriverName == "MoltenVK");
		}

		SUBCASE("The environment variable forces another driver, case-insensitively and by part of its name")
		{
			const VulkanDeviceChoice choice = ChooseVulkanDevice(mac, "kosmic");
			REQUIRE(choice.Index);
			CHECK(mac[*choice.Index].DriverName == "KosmicKrisp");
			CHECK(choice.Forced);
			CHECK(ChooseVulkanDevice(mac, "MOLTENVK").Forced);
		}

		SUBCASE("An unknown or unsuitable requested driver falls back to the default choice")
		{
			VulkanDeviceChoice choice = ChooseVulkanDevice(mac, "radv");
			REQUIRE(choice.Index);
			CHECK(mac[*choice.Index].DriverName == "MoltenVK");
			CHECK(choice.RequestIgnored);
			CHECK_FALSE(choice.Forced);

			std::vector<VulkanDeviceCandidate> broken = mac;
			broken[0].Suitable = false;
			choice = ChooseVulkanDevice(broken, "kosmickrisp");
			REQUIRE(choice.Index);
			CHECK(*choice.Index == 1);
			CHECK(choice.RequestIgnored);
		}

		SUBCASE("A discrete GPU beats the preferred driver on an integrated one; other ties keep the first device")
		{
			const std::vector<VulkanDeviceCandidate> pc = {
				{ "Intel UHD", "MoltenVK", VulkanDeviceKind::Integrated, true },
				{ "GeForce", "NVIDIA", VulkanDeviceKind::Discrete, true },
				{ "Radeon", "AMD proprietary driver", VulkanDeviceKind::Discrete, true },
			};
			CHECK(*ChooseVulkanDevice(pc, "").Index == 1);
		}

		SUBCASE("No suitable device, no choice")
		{
			std::vector<VulkanDeviceCandidate> none = mac;
			for (VulkanDeviceCandidate& candidate : none)
				candidate.Suitable = false;
			CHECK_FALSE(ChooseVulkanDevice(none, "").Index);
			CHECK_FALSE(ChooseVulkanDevice({}, "moltenvk").Index);
		}
	}

	TEST_CASE("A Vulkan device reports the driver it runs on")
	{
		if (!RenderDevice::IsGraphicsApiAvailable(GraphicsApi::Vulkan))
			return;
		RenderDeviceSpecification spec;
		spec.Api = GraphicsApi::Vulkan;
		Scope<RenderDevice> device = RenderDevice::Create(spec);
		if (!device)
			return; // a Windows machine without a Vulkan driver (the D3D12 tests cover it)
		CHECK_FALSE(device->GetDriverName().empty());
#ifdef __APPLE__
		// The driver CI runs on, unless a developer asked for another one.
		if (!std::getenv(VulkanDriverEnvironmentVariable))
			CHECK(device->GetDriverName() == "MoltenVK");
#endif
	}
}
