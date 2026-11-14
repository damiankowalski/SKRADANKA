#include "TestHelpers.h"

#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Renderer/SceneRenderer.h"
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
}
