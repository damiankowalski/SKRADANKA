#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Renderer/DebugDraw.h"
#include "Penumbra/Renderer/RenderCamera.h"
#include "Penumbra/Renderer/RendererTypes.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <string>

namespace Penumbra {

	class RenderDevice;
	class Scene;
	class GpuResourceCache;

	struct SceneRendererSettings
	{
		uint32_t CascadeResolution = 2048;
		uint32_t SpotShadowResolution = 1024;
		uint32_t PointShadowResolution = 512;
		float ShadowDistance = 60.0f;
		uint32_t CascadeCount = 4;
		bool DrawOverlay = true;
	};

	struct SceneRendererStatistics
	{
		uint32_t DrawCalls = 0;
		uint32_t Triangles = 0;
		uint32_t Lights = 0;
		uint32_t ShadowViews = 0;
		uint32_t CulledObjects = 0;
	};

	// Physically based forward renderer:
	//   shadows (cascades, spot, point cube maps; PCSS soft filtering) -> depth/normal prepass ->
	//   SSAO -> forward PBR with IBL -> sky -> translucent + smoke -> bloom -> ACES tonemap -> HUD.
	// The final sRGB-encoded image is GetOutputTexture(); Present() copies it into a swap chain.
	class SceneRenderer
	{
	public:
		explicit SceneRenderer(RenderDevice& device, const SceneRendererSettings& settings = {});
		~SceneRenderer();

		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		void SetViewportSize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_Width; }
		uint32_t GetViewportHeight() const { return m_Height; }

		void Render(Scene& scene, const RenderCamera& camera);
		void Present(nvrhi::IFramebuffer* target);

		nvrhi::ITexture* GetOutputTexture() const { return m_Output; }
		DebugDraw& GetDebugDraw() { return m_DebugDraw; }
		const SceneRendererStatistics& GetStatistics() const { return m_Statistics; }
		SceneRendererSettings& GetSettings() { return m_Settings; }

		// Reads the last rendered frame back to the CPU (RGBA8, sRGB encoded).
		bool ReadOutputPixels(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height);
		bool SaveScreenshot(const std::filesystem::path& path);

	private:
		struct Impl;
		Scope<Impl> m_Impl;

		RenderDevice& m_Device;
		SceneRendererSettings m_Settings;
		SceneRendererStatistics m_Statistics;
		DebugDraw m_DebugDraw;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		nvrhi::TextureHandle m_Output;
	};

}
