#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Renderer/DebugDraw.h"
#include "Penumbra/Renderer/RenderCamera.h"
#include "Penumbra/Renderer/RendererTypes.h"
#include "Penumbra/Scene/FogUtils.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <optional>
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
		uint32_t EyeMapResolution = 1024; // fog of war: the player's eye depth cube (per face)
		// Volumetric fog: the froxel grid over the view frustum (columns, rows, depth slices) and how far it reaches.
		uint32_t FogGridWidth = 160;
		uint32_t FogGridHeight = 90;
		uint32_t FogGridDepth = 64;
		float FogDistance = 64.0f;
		float FogUnshadowedLight = Penumbra::FogUnshadowedLight; // share of a lamp without a shadow map that lights the fog (it would shine through walls; perception uses the same, Scene/FogUtils.h)
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
	//   shadows (cascades, spot, point cube maps; PCSS soft filtering) -> volumetric fog (froxels lit by the
	//   lamps, integrated along every view ray) -> depth/normal prepass -> SSAO -> forward PBR with IBL, seen
	//   through the fog -> sky -> translucent + smoke -> bloom -> ACES tonemap -> HUD.
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
		// Milliseconds the GPU spent on a recent Render() (a few frames old: queries are read back
		// once finished). Empty until the first result arrives.
		std::optional<float> GetGpuTimeMs() const;
		SceneRendererSettings& GetSettings() { return m_Settings; }

		// Reads the last rendered frame back to the CPU (RGBA8, sRGB encoded).
		bool ReadOutputPixels(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height);
		// Tests and tools: reads the last frame's integrated volumetric fog back (shaders/FogIntegrate.hlsl), one
		// texel per froxel, x fastest then y then the depth slice: rgb = light scattered in from the eye to the
		// slice's far end, a = transmittance over the same stretch. size is the grid (FogGrid* settings).
		bool ReadFogVolume(std::vector<glm::vec4>& texels, glm::uvec3& size);
		bool SaveScreenshot(const std::filesystem::path& path);

	private:
		struct Impl;
		Scope<Impl> m_Impl;

		SceneRendererSettings m_Settings;
		SceneRendererStatistics m_Statistics;
		DebugDraw m_DebugDraw;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		nvrhi::TextureHandle m_Output;
	};

}
