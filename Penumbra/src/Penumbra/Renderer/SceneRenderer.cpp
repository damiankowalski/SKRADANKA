#include "pnpch.h"
#include "Penumbra/Renderer/SceneRenderer.h"

#include "Penumbra/Animation/AnimationSystem.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Asset/Importers.h"
#include "Penumbra/Core/Timer.h"
#include "Penumbra/Renderer/GpuResourceCache.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Renderer/ShaderLibrary.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scene/LightUtils.h"
#include "Penumbra/UI/Overlay.h"

#include <glm/gtc/matrix_transform.hpp>

#include <stb_easy_font.h>

#include <map>
#include <tuple>

namespace Penumbra {

	namespace {

		constexpr nvrhi::Format DepthFormat = nvrhi::Format::D32;
		constexpr nvrhi::Format NormalFormat = nvrhi::Format::RGBA16_FLOAT;
		constexpr nvrhi::Format AOFormat = nvrhi::Format::R8_UNORM;
		constexpr nvrhi::Format HDRFormat = nvrhi::Format::RGBA16_FLOAT;
		constexpr nvrhi::Format OutputFormat = nvrhi::Format::RGBA8_UNORM;
		constexpr nvrhi::Format CubeFormat = nvrhi::Format::RGBA16_FLOAT;

		constexpr uint32_t EnvironmentSize = 512;
		constexpr uint32_t IrradianceSize = 32;
		constexpr uint32_t PrefilteredSize = 128;
		constexpr uint32_t PrefilteredMips = 6;
		constexpr uint32_t BRDFLutSize = 128;
		constexpr uint32_t MaxBloomMips = 6;
		constexpr float ShadowNearPlane = 0.05f;

		uint32_t MipCount(uint32_t size)
		{
			uint32_t count = 1;
			while (size > 1)
			{
				size /= 2;
				count++;
			}
			return count;
		}

		nvrhi::ShaderHandle LoadShader(nvrhi::IDevice* device, const char* name, nvrhi::ShaderType type)
		{
			const ShaderBytecode bytecode = ShaderLibrary::Find(name, device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN ? ShaderFormat::Spirv : ShaderFormat::Dxil);
			PN_CORE_VERIFY(bytecode, "Shader missing from the shader library");
			return device->createShader(nvrhi::ShaderDesc().setShaderType(type).setDebugName(name), bytecode.Data, bytecode.Size);
		}

		nvrhi::RenderState MakeRenderState(bool depthTest, bool depthWrite, nvrhi::ComparisonFunc depthFunc, nvrhi::RasterCullMode cull,
			bool alphaBlend = false, bool additive = false, bool depthClip = true)
		{
			nvrhi::RenderState state;
			state.depthStencilState.setDepthTestEnable(depthTest).setDepthWriteEnable(depthWrite).setDepthFunc(depthFunc);
			state.rasterState.setCullMode(cull).setFrontCounterClockwise(true).setDepthClipEnable(depthClip);
			auto& target = state.blendState.targets[0];
			if (alphaBlend)
			{
				target.enableBlend()
					.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
					.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
					.setSrcBlendAlpha(nvrhi::BlendFactor::One)
					.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
			}
			else if (additive)
			{
				target.enableBlend().setSrcBlend(nvrhi::BlendFactor::One).setDestBlend(nvrhi::BlendFactor::One);
			}
			return state;
		}

		nvrhi::ViewportState FullViewport(uint32_t width, uint32_t height)
		{
			return nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height)));
		}

		struct DrawItem
		{
			const GpuMesh* Mesh = nullptr;
			nvrhi::IBuffer* VertexBuffer = nullptr; // the mesh's, or the skinned copy of a skinned instance
			uint32_t IndexOffset = 0;
			uint32_t IndexCount = 0;
			GPU::DrawConstants Constants{};
			nvrhi::IBindingSet* MaterialSet = nullptr;
			glm::mat4 World{ 1.0f };
			BoundingBox Bounds;
			bool CastShadows = true;
			bool Translucent = false;
			float CameraDistance = 0.0f;
		};

		struct ShadowView
		{
			glm::mat4 ViewProj;
			nvrhi::IFramebuffer* Framebuffer = nullptr;
			nvrhi::ITexture* Texture = nullptr;
			uint32_t Slice = 0;
			uint32_t Resolution = 0;
		};

		struct SampledTexture
		{
			nvrhi::ITexture* Texture = nullptr;
			int Mip = -1; // -1 = all mips
			nvrhi::TextureDimension Dimension = nvrhi::TextureDimension::Unknown;

			auto Key() const { return std::make_tuple(Texture, Mip, static_cast<int>(Dimension)); }
		};

		// GPU state of one skinned entity: its joint palette and the skinned vertices all passes draw.
		struct SkinInstance
		{
			const MeshData* Source = nullptr;
			nvrhi::BufferHandle Vertices;
			nvrhi::BufferHandle JointMatrices;
			nvrhi::BindingSetHandle BindingSet;
			uint64_t LastUsedFrame = 0;
		};

		struct SkinJob
		{
			SkinInstance* Instance = nullptr;
			std::vector<glm::mat4> JointMatrices;
			uint32_t VertexCount = 0;
		};

		// Skinned instances not drawn for this many frames release their buffers (several scenes,
		// such as the editor and play mode, can share one renderer).
		constexpr uint64_t SkinInstanceLifetime = 120;

	}

	struct SceneRenderer::Impl
	{
		nvrhi::IDevice* Device = nullptr;
		Scope<GpuResourceCache> Cache;
		nvrhi::CommandListHandle CommandList;

		// GPU time of Render(): a few timestamp pairs in flight, read back frames later.
		struct GpuTimer
		{
			nvrhi::TimerQueryHandle Query;
			bool Pending = false;
		};
		std::array<GpuTimer, 4> GpuTimers;
		std::optional<float> GpuTimeMs;

		void CollectGpuTimers()
		{
			for (GpuTimer& timer : GpuTimers)
			{
				if (timer.Pending && Device->pollTimerQuery(timer.Query))
				{
					GpuTimeMs = Device->getTimerQueryTime(timer.Query) * 1000.0f;
					Device->resetTimerQuery(timer.Query);
					timer.Pending = false;
				}
			}
		}

		nvrhi::ITimerQuery* NextGpuTimer()
		{
			for (GpuTimer& timer : GpuTimers)
			{
				if (timer.Pending)
					continue;
				if (!timer.Query)
					timer.Query = Device->createTimerQuery();
				if (!timer.Query)
					return nullptr;
				timer.Pending = true;
				return timer.Query;
			}
			return nullptr; // every query still in flight: this frame goes unmeasured
		}

		nvrhi::ShaderHandle MeshVS, PrepassPS, ForwardPS, ShadowVS, FullscreenVS, SkyVS, SkyPS, SSAOPS, SSAOBlurPS, BloomDownPS, BloomUpPS,
			TonemapPS, BlitPS, SmokePS, Overlay2DVS, Overlay3DVS, OverlayPS, FlameVS, FlamePS;
		nvrhi::ShaderHandle EquirectCS, DownsampleCS, IrradianceCS, PrefilterCS, BRDFCS, SkinningCS;

		nvrhi::BindingLayoutHandle ViewLayout, MaterialLayout, ShadowLayout, PostLayout, IBLLayout, SkinningLayout;
		nvrhi::InputLayoutHandle MeshInput, ShadowInput, OverlayInput;
		nvrhi::SamplerHandle LinearWrap, ShadowSampler, LinearClamp, PointClamp;

		nvrhi::BufferHandle ViewBuffer, LightBuffer, OverlayBuffer;
		size_t OverlayCapacity = 0;

		nvrhi::TextureHandle CascadeShadows, SpotShadows, PointShadows;
		std::vector<nvrhi::FramebufferHandle> CascadeFramebuffers, SpotFramebuffers, PointFramebuffers;
		nvrhi::BindingSetHandle ShadowSet;

		nvrhi::TextureHandle Depth, Normals, AO, AOTemp, HDR, Bloom;
		uint32_t BloomMips = 1;
		nvrhi::FramebufferHandle PrepassFramebuffer, ForwardFramebuffer, HDRFramebuffer, AOFramebuffer, AOTempFramebuffer, OutputFramebuffer,
			OutputDepthFramebuffer;
		std::vector<nvrhi::FramebufferHandle> BloomFramebuffers;

		nvrhi::GraphicsPipelineHandle ShadowPipeline, PrepassPipeline, ForwardPipeline, TranslucentPipeline, SkyPipeline, SSAOPipeline,
			SSAOBlurPipeline, BloomDownPipeline, BloomUpPipeline, TonemapPipeline, SmokePipeline, Overlay2DPipeline, Overlay3DPipeline, OverlayWorldPipeline, FlamePipeline;
		std::map<nvrhi::Format, nvrhi::GraphicsPipelineHandle> BlitPipelines;
		nvrhi::ComputePipelineHandle EquirectPipeline, DownsamplePipeline, IrradiancePipeline, PrefilterPipeline, BRDFPipeline, SkinningPipeline;

		nvrhi::TextureHandle BRDFLut, EnvironmentCube, IrradianceCube, PrefilteredCube, DummyUAVArray, DummyUAV2D;
		std::string EnvironmentPath;
		uint64_t EnvironmentGeneration = 0;
		bool HasEnvironment = false;

		nvrhi::BindingSetHandle ViewSet;
		bool ViewSetDirty = true;
		std::map<std::array<nvrhi::ITexture*, 4>, nvrhi::BindingSetHandle> MaterialSets;
		std::map<std::tuple<std::tuple<nvrhi::ITexture*, int, int>, std::tuple<nvrhi::ITexture*, int, int>, std::tuple<nvrhi::ITexture*, int, int>>,
			nvrhi::BindingSetHandle>
			PostSets;
		uint64_t AssetGeneration = 0;

		std::vector<DrawItem> Draws;
		std::map<std::pair<const Scene*, entt::entity>, SkinInstance> SkinInstances;
		std::vector<SkinJob> SkinJobs;
		uint64_t FrameIndex = 0;
		std::vector<GPU::Light> Lights;
		GPU::ViewConstants View{};
		Timer Clock; // animates effects in the editor, where scene time stands still

		void CreateShadersAndLayouts();
		void CreatePipelines();
		void CreateShadowMaps(const SceneRendererSettings& settings);
		void CreateTargets(uint32_t width, uint32_t height, nvrhi::TextureHandle& output);
		void CreateBRDFLut();

		nvrhi::IBindingSet* GetMaterialSet(nvrhi::ITexture* albedo, nvrhi::ITexture* normal, nvrhi::ITexture* metallicRoughness, nvrhi::ITexture* emissive);
		nvrhi::IBindingSet* GetPostSet(SampledTexture t0 = {}, SampledTexture t1 = {}, SampledTexture t2 = {});
		nvrhi::IBindingSet* GetViewSet();
		void UpdateEnvironment(const EnvironmentComponent* environment);
		void BuildIBL(const TextureData& equirect);

		void CollectDraws(Scene& scene, const RenderCamera& camera, SceneRendererStatistics& statistics);
		SkinInstance* GetSkinInstance(const Scene& scene, entt::entity entity, const std::shared_ptr<const MeshData>& mesh, const GpuMesh& gpuMesh);
		void RunSkinning();
		void CollectLights(Scene& scene, const RenderCamera& camera, const SceneRendererSettings& settings, std::vector<ShadowView>& shadowViews);
		void ComputeCascades(const RenderCamera& camera, const glm::vec3& lightDirection, const SceneRendererSettings& settings);

		void DrawFullscreen(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer, nvrhi::IBindingSet* set,
			const GPU::PostConstants& constants, uint32_t width, uint32_t height);
	};

	// ------------------------------------------------------------------------------------------
	// Setup
	// ------------------------------------------------------------------------------------------

	void SceneRenderer::Impl::CreateShadersAndLayouts()
	{
		using ST = nvrhi::ShaderType;
		MeshVS = LoadShader(Device, "MeshVS", ST::Vertex);
		PrepassPS = LoadShader(Device, "MeshPrepassPS", ST::Pixel);
		ForwardPS = LoadShader(Device, "MeshForwardPS", ST::Pixel);
		ShadowVS = LoadShader(Device, "ShadowVS", ST::Vertex);
		FullscreenVS = LoadShader(Device, "FullscreenVS", ST::Vertex);
		SkyVS = LoadShader(Device, "SkyVS", ST::Vertex);
		SkyPS = LoadShader(Device, "SkyPS", ST::Pixel);
		SSAOPS = LoadShader(Device, "SSAOPS", ST::Pixel);
		SSAOBlurPS = LoadShader(Device, "SSAOBlurPS", ST::Pixel);
		BloomDownPS = LoadShader(Device, "BloomDownsamplePS", ST::Pixel);
		BloomUpPS = LoadShader(Device, "BloomUpsamplePS", ST::Pixel);
		TonemapPS = LoadShader(Device, "TonemapPS", ST::Pixel);
		BlitPS = LoadShader(Device, "BlitPS", ST::Pixel);
		SmokePS = LoadShader(Device, "SmokePS", ST::Pixel);
		FlameVS = LoadShader(Device, "FlameVS", ST::Vertex);
		FlamePS = LoadShader(Device, "FlamePS", ST::Pixel);
		Overlay2DVS = LoadShader(Device, "Overlay2DVS", ST::Vertex);
		Overlay3DVS = LoadShader(Device, "Overlay3DVS", ST::Vertex);
		OverlayPS = LoadShader(Device, "OverlayPS", ST::Pixel);
		EquirectCS = LoadShader(Device, "EquirectToCubeCS", ST::Compute);
		DownsampleCS = LoadShader(Device, "CubeDownsampleCS", ST::Compute);
		IrradianceCS = LoadShader(Device, "IrradianceCS", ST::Compute);
		PrefilterCS = LoadShader(Device, "PrefilterCS", ST::Compute);
		BRDFCS = LoadShader(Device, "BRDFLutCS", ST::Compute);
		SkinningCS = LoadShader(Device, "SkinningCS", ST::Compute);

		{
			nvrhi::BindingLayoutDesc desc;
			desc.registerSpaceIsDescriptorSet = true; // HLSL space N = Vulkan descriptor set N
			desc.visibility = ST::All;
			desc.registerSpace = 0;
			desc.bindings = { nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_SRV(4), nvrhi::BindingLayoutItem::Texture_SRV(5), nvrhi::BindingLayoutItem::Texture_SRV(6),
				nvrhi::BindingLayoutItem::Texture_SRV(7), nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Sampler(1),
				nvrhi::BindingLayoutItem::Sampler(2), nvrhi::BindingLayoutItem::Sampler(3) };
			ViewLayout = Device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc;
			desc.registerSpaceIsDescriptorSet = true; // HLSL space N = Vulkan descriptor set N
			desc.visibility = ST::All;
			desc.registerSpace = 1;
			desc.bindings = { nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GPU::DrawConstants)), nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Texture_SRV(3) };
			MaterialLayout = Device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc;
			desc.registerSpaceIsDescriptorSet = true; // HLSL space N = Vulkan descriptor set N
			desc.visibility = ST::Vertex;
			desc.bindings = { nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GPU::ShadowConstants)) };
			ShadowLayout = Device->createBindingLayout(desc);
			ShadowSet = Device->createBindingSet(
				nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GPU::ShadowConstants))), ShadowLayout);
		}
		{
			nvrhi::BindingLayoutDesc desc;
			desc.registerSpaceIsDescriptorSet = true; // HLSL space N = Vulkan descriptor set N
			desc.visibility = ST::All;
			desc.bindings = { nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::PushConstants(1, sizeof(GPU::PostConstants)),
				nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Sampler(1) };
			PostLayout = Device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc;
			desc.registerSpaceIsDescriptorSet = true; // HLSL space N = Vulkan descriptor set N
			desc.visibility = ST::Compute;
			desc.bindings = { nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GPU::IBLConstants)), nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1), nvrhi::BindingLayoutItem::Texture_SRV(2), nvrhi::BindingLayoutItem::Texture_UAV(0),
				nvrhi::BindingLayoutItem::Texture_UAV(1), nvrhi::BindingLayoutItem::Sampler(0) };
			IBLLayout = Device->createBindingLayout(desc);
		}
		{
			nvrhi::BindingLayoutDesc desc;
			desc.registerSpaceIsDescriptorSet = true; // HLSL space N = Vulkan descriptor set N
			desc.visibility = ST::Compute;
			desc.bindings = { nvrhi::BindingLayoutItem::PushConstants(0, sizeof(GPU::SkinningConstants)),
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0), nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2), nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0) };
			SkinningLayout = Device->createBindingLayout(desc);
		}

		const nvrhi::VertexAttributeDesc meshAttributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Normal)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(Vertex, Tangent)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, TexCoord)).setElementStride(sizeof(Vertex)),
		};
		MeshInput = Device->createInputLayout(meshAttributes, 4, MeshVS);
		ShadowInput = Device->createInputLayout(meshAttributes, 1, ShadowVS);

		const nvrhi::VertexAttributeDesc overlayAttributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(GPU::OverlayVertex, Position)).setElementStride(sizeof(GPU::OverlayVertex)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(GPU::OverlayVertex, Color)).setElementStride(sizeof(GPU::OverlayVertex)),
		};
		OverlayInput = Device->createInputLayout(overlayAttributes, 2, Overlay2DVS);

		LinearWrap = Device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Wrap).setMaxAnisotropy(8.0f));
		ShadowSampler = Device->createSampler(nvrhi::SamplerDesc()
				.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
				.setReductionType(nvrhi::SamplerReductionType::Comparison));
		LinearClamp = Device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		PointClamp = Device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp).setAllFilters(false));

		ViewBuffer = Device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(sizeof(GPU::ViewConstants))
				.setIsConstantBuffer(true)
				.setIsVolatile(true)
				.setMaxVersions(64)
				.setDebugName("ViewConstants"));
		LightBuffer = Device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(sizeof(GPU::Light) * GPU::MaxLights)
				.setStructStride(sizeof(GPU::Light))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setDebugName("Lights"));
	}

	void SceneRenderer::Impl::CreatePipelines()
	{
		using CF = nvrhi::ComparisonFunc;
		using Cull = nvrhi::RasterCullMode;

		auto graphics = [&](nvrhi::IShader* vs, nvrhi::IShader* ps, nvrhi::IInputLayout* input, std::initializer_list<nvrhi::IBindingLayout*> layouts,
							const nvrhi::RenderState& state, const nvrhi::FramebufferInfo& info,
							nvrhi::PrimitiveType primitive = nvrhi::PrimitiveType::TriangleList) {
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = vs;
			desc.PS = ps;
			desc.inputLayout = input;
			desc.primType = primitive;
			desc.renderState = state;
			for (nvrhi::IBindingLayout* layout : layouts)
				desc.bindingLayouts.push_back(layout);
			nvrhi::GraphicsPipelineHandle pipeline = Device->createGraphicsPipeline(desc, info);
			PN_CORE_VERIFY(pipeline, "Failed to create a graphics pipeline");
			return pipeline;
		};

		const auto depthOnly = nvrhi::FramebufferInfo().setDepthFormat(DepthFormat);
		const auto prepass = nvrhi::FramebufferInfo().addColorFormat(NormalFormat).setDepthFormat(DepthFormat);
		const auto hdrDepth = nvrhi::FramebufferInfo().addColorFormat(HDRFormat).setDepthFormat(DepthFormat);
		const auto hdr = nvrhi::FramebufferInfo().addColorFormat(HDRFormat);
		const auto ao = nvrhi::FramebufferInfo().addColorFormat(AOFormat);
		const auto output = nvrhi::FramebufferInfo().addColorFormat(OutputFormat);
		const auto outputDepth = nvrhi::FramebufferInfo().addColorFormat(OutputFormat).setDepthFormat(DepthFormat);

		nvrhi::RenderState shadowState = MakeRenderState(true, true, CF::LessOrEqual, Cull::None, false, false, false);
		shadowState.rasterState.depthBias = 2000;
		shadowState.rasterState.slopeScaledDepthBias = 2.0f;
		shadowState.rasterState.depthBiasClamp = 0.01f;
		ShadowPipeline = graphics(ShadowVS, nullptr, ShadowInput, { ShadowLayout }, shadowState, depthOnly);

		PrepassPipeline = graphics(MeshVS, PrepassPS, MeshInput, { ViewLayout, MaterialLayout }, MakeRenderState(true, true, CF::Less, Cull::Back), prepass);
		ForwardPipeline = graphics(MeshVS, ForwardPS, MeshInput, { ViewLayout, MaterialLayout }, MakeRenderState(true, false, CF::LessOrEqual, Cull::Back), hdrDepth);
		TranslucentPipeline = graphics(MeshVS, ForwardPS, MeshInput, { ViewLayout, MaterialLayout },
			MakeRenderState(true, false, CF::LessOrEqual, Cull::None, true), hdrDepth);
		SkyPipeline = graphics(SkyVS, SkyPS, nullptr, { PostLayout }, MakeRenderState(true, false, CF::LessOrEqual, Cull::None), hdrDepth);

		const nvrhi::RenderState post = MakeRenderState(false, false, CF::Always, Cull::None);
		SSAOPipeline = graphics(FullscreenVS, SSAOPS, nullptr, { PostLayout }, post, ao);
		SSAOBlurPipeline = graphics(FullscreenVS, SSAOBlurPS, nullptr, { PostLayout }, post, ao);
		BloomDownPipeline = graphics(FullscreenVS, BloomDownPS, nullptr, { PostLayout }, post, hdr);
		BloomUpPipeline = graphics(FullscreenVS, BloomUpPS, nullptr, { PostLayout }, MakeRenderState(false, false, CF::Always, Cull::None, false, true), hdr);
		TonemapPipeline = graphics(FullscreenVS, TonemapPS, nullptr, { PostLayout }, post, output);
		FlamePipeline = graphics(FlameVS, FlamePS, nullptr, { PostLayout }, MakeRenderState(true, false, CF::LessOrEqual, Cull::None, false, true), hdrDepth);
		SmokePipeline = graphics(FullscreenVS, SmokePS, nullptr, { PostLayout }, MakeRenderState(false, false, CF::Always, Cull::None, true), hdr);
		Overlay2DPipeline = graphics(Overlay2DVS, OverlayPS, OverlayInput, { PostLayout }, MakeRenderState(false, false, CF::Always, Cull::None, true), output);
		Overlay3DPipeline = graphics(Overlay3DVS, OverlayPS, OverlayInput, { PostLayout },
			MakeRenderState(true, false, CF::LessOrEqual, Cull::None, true), outputDepth, nvrhi::PrimitiveType::LineList);
		OverlayWorldPipeline = graphics(Overlay3DVS, OverlayPS, OverlayInput, { PostLayout },
			MakeRenderState(true, false, CF::LessOrEqual, Cull::None, true), outputDepth);

		auto compute = [&](nvrhi::IShader* cs) {
			nvrhi::ComputePipelineDesc desc;
			desc.CS = cs;
			desc.bindingLayouts = { IBLLayout };
			nvrhi::ComputePipelineHandle pipeline = Device->createComputePipeline(desc);
			PN_CORE_VERIFY(pipeline, "Failed to create a compute pipeline");
			return pipeline;
		};
		EquirectPipeline = compute(EquirectCS);
		DownsamplePipeline = compute(DownsampleCS);
		IrradiancePipeline = compute(IrradianceCS);
		PrefilterPipeline = compute(PrefilterCS);
		BRDFPipeline = compute(BRDFCS);

		nvrhi::ComputePipelineDesc skinning;
		skinning.CS = SkinningCS;
		skinning.bindingLayouts = { SkinningLayout };
		SkinningPipeline = Device->createComputePipeline(skinning);
		PN_CORE_VERIFY(SkinningPipeline, "Failed to create the skinning pipeline");
	}

	void SceneRenderer::Impl::CreateShadowMaps(const SceneRendererSettings& settings)
	{
		auto create = [&](const char* name, uint32_t resolution, uint32_t slices, nvrhi::TextureDimension dimension,
						  std::vector<nvrhi::FramebufferHandle>& framebuffers) {
			nvrhi::TextureDesc desc;
			desc.width = resolution;
			desc.height = resolution;
			desc.arraySize = slices;
			desc.dimension = dimension;
			desc.format = DepthFormat;
			desc.isRenderTarget = true;
			desc.debugName = name;
			desc.setClearValue(nvrhi::Color(1.0f));
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			nvrhi::TextureHandle texture = Device->createTexture(desc);
			for (uint32_t slice = 0; slice < slices; slice++)
			{
				framebuffers.push_back(Device->createFramebuffer(
					nvrhi::FramebufferDesc().setDepthAttachment(texture, nvrhi::TextureSubresourceSet(0, 1, slice, 1))));
			}
			return texture;
		};
		CascadeShadows = create("CascadeShadows", settings.CascadeResolution, GPU::MaxCascades, nvrhi::TextureDimension::Texture2DArray, CascadeFramebuffers);
		SpotShadows = create("SpotShadows", settings.SpotShadowResolution, GPU::MaxSpotShadows, nvrhi::TextureDimension::Texture2DArray, SpotFramebuffers);
		PointShadows = create("PointShadows", settings.PointShadowResolution, GPU::MaxPointShadows * 6, nvrhi::TextureDimension::TextureCubeArray, PointFramebuffers);
	}

	void SceneRenderer::Impl::CreateTargets(uint32_t width, uint32_t height, nvrhi::TextureHandle& output)
	{
		auto target = [&](const char* name, nvrhi::Format format, uint32_t w, uint32_t h, uint32_t mips, nvrhi::Color clear, bool depth = false) {
			nvrhi::TextureDesc desc;
			desc.width = w;
			desc.height = h;
			desc.mipLevels = mips;
			desc.format = format;
			desc.isRenderTarget = true;
			desc.debugName = name;
			desc.setClearValue(clear);
			desc.initialState = depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
			desc.keepInitialState = true;
			return Device->createTexture(desc);
		};

		Depth = target("Depth", DepthFormat, width, height, 1, nvrhi::Color(1.0f), true);
		Normals = target("Normals", NormalFormat, width, height, 1, nvrhi::Color(0.0f));
		AO = target("AO", AOFormat, width, height, 1, nvrhi::Color(1.0f));
		AOTemp = target("AOTemp", AOFormat, width, height, 1, nvrhi::Color(1.0f));
		HDR = target("HDR", HDRFormat, width, height, 1, nvrhi::Color(0.0f));
		output = target("Output", OutputFormat, width, height, 1, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));

		const uint32_t bloomWidth = std::max(width / 2, 1u), bloomHeight = std::max(height / 2, 1u);
		BloomMips = std::clamp(MipCount(std::min(bloomWidth, bloomHeight)) - 2, 1u, MaxBloomMips);
		Bloom = target("Bloom", HDRFormat, bloomWidth, bloomHeight, BloomMips, nvrhi::Color(0.0f));

		PrepassFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(Normals).setDepthAttachment(Depth));
		ForwardFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(HDR).setDepthAttachment(Depth));
		HDRFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(HDR));
		AOFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(AO));
		AOTempFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(AOTemp));
		OutputFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(output));
		OutputDepthFramebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(output).setDepthAttachment(Depth));
		BloomFramebuffers.clear();
		for (uint32_t mip = 0; mip < BloomMips; mip++)
			BloomFramebuffers.push_back(Device->createFramebuffer(
				nvrhi::FramebufferDesc().addColorAttachment(Bloom, nvrhi::TextureSubresourceSet(mip, 1, 0, 1))));

		PostSets.clear();
		ViewSetDirty = true;
	}

	void SceneRenderer::Impl::CreateBRDFLut()
	{
		auto storage = [&](const char* name, nvrhi::Format format, uint32_t size, uint32_t slices, nvrhi::TextureDimension dimension, uint32_t mips) {
			nvrhi::TextureDesc desc;
			desc.width = size;
			desc.height = size;
			desc.arraySize = slices;
			desc.dimension = dimension;
			desc.mipLevels = mips;
			desc.format = format;
			desc.isUAV = true;
			desc.debugName = name;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			return Device->createTexture(desc);
		};
		BRDFLut = storage("BRDFLut", nvrhi::Format::RG16_FLOAT, BRDFLutSize, 1, nvrhi::TextureDimension::Texture2D, 1);
		DummyUAVArray = storage("DummyUAVArray", CubeFormat, 1, 6, nvrhi::TextureDimension::Texture2DArray, 1);
		DummyUAV2D = storage("DummyUAV2D", nvrhi::Format::RG16_FLOAT, 1, 1, nvrhi::TextureDimension::Texture2D, 1);
		EnvironmentCube = storage("EnvironmentCube", CubeFormat, EnvironmentSize, 6, nvrhi::TextureDimension::TextureCube, MipCount(EnvironmentSize));
		IrradianceCube = storage("IrradianceCube", CubeFormat, IrradianceSize, 6, nvrhi::TextureDimension::TextureCube, 1);
		PrefilteredCube = storage("PrefilteredCube", CubeFormat, PrefilteredSize, 6, nvrhi::TextureDimension::TextureCube, PrefilteredMips);

		nvrhi::BindingSetDesc desc;
		desc.bindings = { nvrhi::BindingSetItem::PushConstants(0, sizeof(GPU::IBLConstants)),
			nvrhi::BindingSetItem::Texture_SRV(0, Cache->GetBlackTexture()), nvrhi::BindingSetItem::Texture_SRV(1, Cache->GetBlackCubeTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, Cache->GetBlackCubeTexture(), nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray),
			nvrhi::BindingSetItem::Texture_UAV(0, DummyUAVArray, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(0, 1, 0, 6), nvrhi::TextureDimension::Texture2DArray),
			nvrhi::BindingSetItem::Texture_UAV(1, BRDFLut), nvrhi::BindingSetItem::Sampler(0, LinearClamp) };
		nvrhi::BindingSetHandle set = Device->createBindingSet(desc, IBLLayout);

		CommandList->open();
		CommandList->setComputeState(nvrhi::ComputeState().setPipeline(BRDFPipeline).addBindingSet(set));
		const GPU::IBLConstants constants{ BRDFLutSize, 0.0f, 0.0f, 256 };
		CommandList->setPushConstants(&constants, sizeof(constants));
		CommandList->dispatch((BRDFLutSize + 7) / 8, (BRDFLutSize + 7) / 8, 1);
		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	// ------------------------------------------------------------------------------------------
	// Binding sets
	// ------------------------------------------------------------------------------------------

	nvrhi::IBindingSet* SceneRenderer::Impl::GetMaterialSet(nvrhi::ITexture* albedo, nvrhi::ITexture* normal, nvrhi::ITexture* metallicRoughness,
		nvrhi::ITexture* emissive)
	{
		const std::array<nvrhi::ITexture*, 4> key = { albedo, normal, metallicRoughness, emissive };
		if (const auto it = MaterialSets.find(key); it != MaterialSets.end())
			return it->second;
		nvrhi::BindingSetDesc desc;
		desc.bindings = { nvrhi::BindingSetItem::PushConstants(0, sizeof(GPU::DrawConstants)), nvrhi::BindingSetItem::Texture_SRV(0, albedo),
			nvrhi::BindingSetItem::Texture_SRV(1, normal), nvrhi::BindingSetItem::Texture_SRV(2, metallicRoughness),
			nvrhi::BindingSetItem::Texture_SRV(3, emissive) };
		nvrhi::BindingSetHandle set = Device->createBindingSet(desc, MaterialLayout);
		return MaterialSets.emplace(key, set).first->second;
	}

	nvrhi::IBindingSet* SceneRenderer::Impl::GetPostSet(SampledTexture t0, SampledTexture t1, SampledTexture t2)
	{
		auto fill = [&](SampledTexture& t) {
			if (!t.Texture)
				t.Texture = Cache->GetBlackTexture();
		};
		fill(t0);
		fill(t1);
		fill(t2);
		const auto key = std::make_tuple(t0.Key(), t1.Key(), t2.Key());
		if (const auto it = PostSets.find(key); it != PostSets.end())
			return it->second;

		auto item = [](uint32_t slot, const SampledTexture& t) {
			const nvrhi::TextureSubresourceSet subresources = t.Mip < 0 ? nvrhi::AllSubresources
																		: nvrhi::TextureSubresourceSet(static_cast<uint32_t>(t.Mip), 1, 0, 1);
			return nvrhi::BindingSetItem::Texture_SRV(slot, t.Texture, nvrhi::Format::UNKNOWN, subresources, t.Dimension);
		};
		nvrhi::BindingSetDesc desc;
		desc.bindings = { nvrhi::BindingSetItem::ConstantBuffer(0, ViewBuffer), nvrhi::BindingSetItem::PushConstants(1, sizeof(GPU::PostConstants)),
			item(0, t0), item(1, t1), item(2, t2), nvrhi::BindingSetItem::Sampler(0, LinearClamp), nvrhi::BindingSetItem::Sampler(1, PointClamp) };
		nvrhi::BindingSetHandle set = Device->createBindingSet(desc, PostLayout);
		return PostSets.emplace(key, set).first->second;
	}

	nvrhi::IBindingSet* SceneRenderer::Impl::GetViewSet()
	{
		if (!ViewSetDirty && ViewSet)
			return ViewSet;
		nvrhi::ITexture* irradiance = HasEnvironment ? IrradianceCube.Get() : Cache->GetBlackCubeTexture();
		nvrhi::ITexture* prefiltered = HasEnvironment ? PrefilteredCube.Get() : Cache->GetBlackCubeTexture();
		nvrhi::BindingSetDesc desc;
		desc.bindings = { nvrhi::BindingSetItem::ConstantBuffer(0, ViewBuffer), nvrhi::BindingSetItem::StructuredBuffer_SRV(0, LightBuffer),
			nvrhi::BindingSetItem::Texture_SRV(1, CascadeShadows), nvrhi::BindingSetItem::Texture_SRV(2, SpotShadows),
			nvrhi::BindingSetItem::Texture_SRV(3, PointShadows), nvrhi::BindingSetItem::Texture_SRV(4, irradiance),
			nvrhi::BindingSetItem::Texture_SRV(5, prefiltered), nvrhi::BindingSetItem::Texture_SRV(6, BRDFLut),
			nvrhi::BindingSetItem::Texture_SRV(7, AO), nvrhi::BindingSetItem::Sampler(0, LinearWrap),
			nvrhi::BindingSetItem::Sampler(1, ShadowSampler), nvrhi::BindingSetItem::Sampler(2, LinearClamp),
			nvrhi::BindingSetItem::Sampler(3, PointClamp) };
		ViewSet = Device->createBindingSet(desc, ViewLayout);
		ViewSetDirty = false;
		return ViewSet;
	}

	// ------------------------------------------------------------------------------------------
	// Image-based lighting
	// ------------------------------------------------------------------------------------------

	void SceneRenderer::Impl::BuildIBL(const TextureData& equirect)
	{
		nvrhi::ITexture* source = Cache->GetTexture(&equirect, false, CommandList);
		if (!source)
			return;

		auto set = [&](nvrhi::ITexture* t0, nvrhi::ITexture* t1, nvrhi::ITexture* t2, int t2Mip, nvrhi::ITexture* output, uint32_t outputMip) {
			nvrhi::BindingSetDesc desc;
			desc.bindings = { nvrhi::BindingSetItem::PushConstants(0, sizeof(GPU::IBLConstants)),
				nvrhi::BindingSetItem::Texture_SRV(0, t0 ? t0 : Cache->GetBlackTexture()),
				nvrhi::BindingSetItem::Texture_SRV(1, t1 ? t1 : Cache->GetBlackCubeTexture()),
				nvrhi::BindingSetItem::Texture_SRV(2, t2 ? t2 : Cache->GetBlackCubeTexture(), nvrhi::Format::UNKNOWN,
					t2 ? nvrhi::TextureSubresourceSet(static_cast<uint32_t>(t2Mip), 1, 0, 6) : nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray),
				nvrhi::BindingSetItem::Texture_UAV(0, output, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(outputMip, 1, 0, 6),
					nvrhi::TextureDimension::Texture2DArray),
				nvrhi::BindingSetItem::Texture_UAV(1, DummyUAV2D), nvrhi::BindingSetItem::Sampler(0, LinearClamp) };
			return Device->createBindingSet(desc, IBLLayout);
		};
		auto dispatch = [&](nvrhi::IComputePipeline* pipeline, nvrhi::IBindingSet* bindingSet, const GPU::IBLConstants& constants) {
			CommandList->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(bindingSet));
			CommandList->setPushConstants(&constants, sizeof(constants));
			CommandList->dispatch((constants.FaceSize + 7) / 8, (constants.FaceSize + 7) / 8, 6);
		};

		// 1. Equirectangular -> cube, then a box-filtered mip chain.
		dispatch(EquirectPipeline, set(source, nullptr, nullptr, 0, EnvironmentCube, 0), { EnvironmentSize, 0.0f, 0.0f, 0 });
		const uint32_t environmentMips = MipCount(EnvironmentSize);
		for (uint32_t mip = 1; mip < environmentMips; mip++)
		{
			const uint32_t size = std::max(EnvironmentSize >> mip, 1u);
			dispatch(DownsamplePipeline, set(nullptr, nullptr, EnvironmentCube, static_cast<int>(mip - 1), EnvironmentCube, mip), { size, 0.0f, 0.0f, 0 });
		}

		// 2. Diffuse irradiance from a low mip (it is a very wide filter anyway).
		dispatch(IrradiancePipeline, set(nullptr, EnvironmentCube, nullptr, 0, IrradianceCube, 0), { IrradianceSize, 0.0f, 4.0f, 0 });

		// 3. Specular prefiltered radiance, one roughness per mip.
		for (uint32_t mip = 0; mip < PrefilteredMips; mip++)
		{
			const float roughness = static_cast<float>(mip) / static_cast<float>(PrefilteredMips - 1);
			const uint32_t size = std::max(PrefilteredSize >> mip, 1u);
			const float sourceMip = mip == 0 ? 0.0f : 1.0f + roughness * 5.0f;
			dispatch(PrefilterPipeline, set(nullptr, EnvironmentCube, nullptr, 0, PrefilteredCube, mip), { size, roughness, sourceMip, 128 });
		}
	}

	void SceneRenderer::Impl::UpdateEnvironment(const EnvironmentComponent* environment)
	{
		const std::string path = environment ? environment->EnvironmentMap : std::string();
		const uint64_t generation = AssetManager::GetGeneration();
		if (path == EnvironmentPath && (path.empty() || generation == EnvironmentGeneration))
			return;
		EnvironmentPath = path;
		EnvironmentGeneration = generation;
		HasEnvironment = false;
		ViewSetDirty = true;
		if (path.empty())
			return;

		std::shared_ptr<const TextureData> equirect = AssetManager::GetTexture(path);
		if (!equirect)
			return;
		if (equirect->Format != TextureFormat::RGBA32F)
			PN_CORE_WARN("Environment map '{}' is not HDR; lighting will be clamped to LDR range", path);
		// The IBL build reads the texture through the cache; keep it alive for the cache entry.
		Cache->GetTexture(equirect, false, CommandList);
		BuildIBL(*equirect);
		HasEnvironment = true;
		PN_CORE_INFO("Environment lighting built from '{}'", path);
	}

	// ------------------------------------------------------------------------------------------
	// Scene extraction
	// ------------------------------------------------------------------------------------------

	void SceneRenderer::Impl::CollectDraws(Scene& scene, const RenderCamera& camera, SceneRendererStatistics& statistics)
	{
		Draws.clear();
		const Frustum frustum(camera.Projection * camera.View);
		auto& registry = scene.GetRegistry();

		for (auto [handle, meshComponent] : registry.view<MeshComponent>().each())
		{
			if (!meshComponent.Visible)
				continue;
			std::shared_ptr<const MeshData> mesh = AssetManager::GetMesh(meshComponent.Mesh);
			const GpuMesh* gpuMesh = Cache->GetMesh(mesh, CommandList);
			if (!gpuMesh)
				continue;

			Entity entity(handle, &scene);
			const glm::mat4 world = scene.GetWorldTransform(entity);

			// Skinned meshes: pose the skeleton now; the bounds follow the pose.
			BoundingBox bounds = mesh->Bounds;
			std::vector<glm::mat4> jointMatrices;
			const bool skinned = mesh->IsSkinned() && gpuMesh->SkinBuffer;
			if (skinned)
			{
				AnimationSystem::EvaluateSkinMatrices(*mesh, entity.TryGetComponent<AnimatorComponent>(), jointMatrices);
				bounds = Animation::ComputeSkinnedBounds(*mesh, jointMatrices);
			}

			const bool visible = frustum.IntersectsBox(bounds, world);
			if (!visible && !meshComponent.CastShadows)
			{
				statistics.CulledObjects++;
				continue;
			}

			nvrhi::IBuffer* vertexBuffer = gpuMesh->VertexBuffer;
			if (skinned)
			{
				SkinInstance* instance = GetSkinInstance(scene, handle, mesh, *gpuMesh);
				if (!instance)
					continue;
				vertexBuffer = instance->Vertices;
				SkinJobs.push_back({ instance, std::move(jointMatrices), static_cast<uint32_t>(mesh->Vertices.size()) });
			}

			const MaterialComponent* overrideMaterial = entity.TryGetComponent<MaterialComponent>();
			for (const Submesh& submesh : mesh->Submeshes)
			{
				const std::vector<std::string>& hidden = meshComponent.HiddenNodes;
				if (!hidden.empty() && std::find(hidden.begin(), hidden.end(), submesh.Node) != hidden.end())
					continue;
				DrawItem draw;
				draw.Mesh = gpuMesh;
				draw.VertexBuffer = vertexBuffer;
				draw.IndexOffset = submesh.IndexOffset;
				draw.IndexCount = submesh.IndexCount;
				draw.World = world;
				draw.Bounds = bounds;
				draw.CastShadows = meshComponent.CastShadows;
				draw.CameraDistance = glm::distance(glm::vec3(world[3]), camera.Position);
				GPU::PackWorldRows(world, draw.Constants.WorldRow0, draw.Constants.WorldRow1, draw.Constants.WorldRow2);

				nvrhi::ITexture* albedo = nullptr;
				nvrhi::ITexture* normal = nullptr;
				nvrhi::ITexture* metallicRoughness = nullptr;
				nvrhi::ITexture* emissive = nullptr;
				if (overrideMaterial)
				{
					draw.Constants.BaseColor = overrideMaterial->BaseColor;
					draw.Constants.Metallic = overrideMaterial->Metallic;
					draw.Constants.Roughness = overrideMaterial->Roughness;
					draw.Constants.Emissive = overrideMaterial->EmissiveEnabled ? overrideMaterial->EmissiveColor * overrideMaterial->EmissiveIntensity : glm::vec3(0.0f);
					draw.Translucent = overrideMaterial->Translucent;
					albedo = Cache->GetTexture(AssetManager::GetTexture(overrideMaterial->AlbedoTexture), true, CommandList);
					normal = Cache->GetTexture(AssetManager::GetTexture(overrideMaterial->NormalTexture), false, CommandList);
					metallicRoughness = Cache->GetTexture(AssetManager::GetTexture(overrideMaterial->MetallicRoughnessTexture), false, CommandList);
					emissive = Cache->GetTexture(AssetManager::GetTexture(overrideMaterial->EmissiveTexture), true, CommandList);
				}
				else if (submesh.MaterialIndex < mesh->Materials.size())
				{
					const MaterialData& material = mesh->Materials[submesh.MaterialIndex];
					draw.Constants.BaseColor = material.BaseColor;
					draw.Constants.Metallic = material.Metallic;
					draw.Constants.Roughness = material.Roughness;
					draw.Constants.Emissive = material.Emissive;
					draw.Translucent = material.AlphaBlend;
					auto meshTexture = [&](TextureIndex index, bool srgb) -> nvrhi::ITexture* {
						if (index < 0 || static_cast<size_t>(index) >= mesh->Textures.size())
							return nullptr;
						return Cache->GetTexture(&mesh->Textures[static_cast<size_t>(index)], srgb, CommandList);
					};
					albedo = meshTexture(material.AlbedoTexture, true);
					normal = meshTexture(material.NormalTexture, false);
					metallicRoughness = meshTexture(material.MetallicRoughnessTexture, false);
					emissive = meshTexture(material.EmissiveTexture, true);
				}
				else
				{
					draw.Constants.BaseColor = glm::vec4(1.0f);
					draw.Constants.Roughness = 0.6f;
				}

				draw.Constants.TextureFlags = (albedo ? GPU::HasAlbedo : 0u) | (normal ? GPU::HasNormal : 0u) |
											  (metallicRoughness ? GPU::HasMetallicRoughness : 0u) | (emissive ? GPU::HasEmissive : 0u);
				draw.MaterialSet = GetMaterialSet(albedo ? albedo : Cache->GetWhiteTexture(), normal ? normal : Cache->GetFlatNormalTexture(),
					metallicRoughness ? metallicRoughness : Cache->GetWhiteTexture(), emissive ? emissive : Cache->GetWhiteTexture());
				Draws.push_back(draw);
			}
		}
	}

	SkinInstance* SceneRenderer::Impl::GetSkinInstance(const Scene& scene, entt::entity entity, const std::shared_ptr<const MeshData>& mesh,
		const GpuMesh& gpuMesh)
	{
		const std::pair<const Scene*, entt::entity> key{ &scene, entity };
		SkinInstance& instance = SkinInstances[key];
		instance.LastUsedFrame = FrameIndex;
		if (instance.Source == mesh.get() && instance.BindingSet)
			return &instance;

		instance = SkinInstance{};
		instance.LastUsedFrame = FrameIndex;
		nvrhi::BufferDesc vertexDesc;
		vertexDesc.byteSize = mesh->Vertices.size() * sizeof(Vertex);
		vertexDesc.structStride = sizeof(Vertex);
		vertexDesc.isVertexBuffer = true;
		vertexDesc.canHaveUAVs = true;
		vertexDesc.debugName = mesh->Name + ".SkinnedVertices";
		vertexDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
		vertexDesc.keepInitialState = true;
		instance.Vertices = Device->createBuffer(vertexDesc);

		nvrhi::BufferDesc jointDesc;
		jointDesc.byteSize = std::max<size_t>(mesh->Joints.size(), 1) * sizeof(glm::mat4);
		jointDesc.structStride = sizeof(glm::mat4);
		jointDesc.debugName = mesh->Name + ".JointMatrices";
		jointDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		jointDesc.keepInitialState = true;
		instance.JointMatrices = Device->createBuffer(jointDesc);
		if (!instance.Vertices || !instance.JointMatrices)
		{
			SkinInstances.erase(key);
			return nullptr;
		}

		nvrhi::BindingSetDesc desc;
		desc.bindings = { nvrhi::BindingSetItem::PushConstants(0, sizeof(GPU::SkinningConstants)),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, gpuMesh.VertexBuffer), nvrhi::BindingSetItem::StructuredBuffer_SRV(1, gpuMesh.SkinBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, instance.JointMatrices), nvrhi::BindingSetItem::StructuredBuffer_UAV(0, instance.Vertices) };
		instance.BindingSet = Device->createBindingSet(desc, SkinningLayout);
		instance.Source = mesh.get();
		return &instance;
	}

	void SceneRenderer::Impl::RunSkinning()
	{
		for (const SkinJob& job : SkinJobs)
		{
			if (job.JointMatrices.empty())
				continue;
			CommandList->writeBuffer(job.Instance->JointMatrices, job.JointMatrices.data(), job.JointMatrices.size() * sizeof(glm::mat4));
			CommandList->setComputeState(nvrhi::ComputeState().setPipeline(SkinningPipeline).addBindingSet(job.Instance->BindingSet));
			const GPU::SkinningConstants constants{ job.VertexCount, static_cast<uint32_t>(job.JointMatrices.size()), { 0, 0 } };
			CommandList->setPushConstants(&constants, sizeof(constants));
			CommandList->dispatch((job.VertexCount + 63) / 64, 1, 1);
		}
		SkinJobs.clear();

		// Release instances of entities that are gone (or have not been rendered for a while).
		std::erase_if(SkinInstances, [&](const auto& entry) { return FrameIndex - entry.second.LastUsedFrame > SkinInstanceLifetime; });
	}

	void SceneRenderer::Impl::ComputeCascades(const RenderCamera& camera, const glm::vec3& lightDirection, const SceneRendererSettings& settings)
	{
		const uint32_t cascades = std::clamp(settings.CascadeCount, 1u, GPU::MaxCascades);
		const float nearClip = camera.NearClip;
		const float farClip = std::min(camera.FarClip, settings.ShadowDistance);
		const float tanHalf = std::tan(glm::radians(camera.VerticalFOV) * 0.5f);
		const glm::mat4 inverseView = glm::inverse(camera.View);
		constexpr float Lambda = 0.8f;

		float previous = nearClip;
		for (uint32_t i = 0; i < GPU::MaxCascades; i++)
		{
			if (i >= cascades)
			{
				View.CascadeSplits[i] = farClip;
				View.CascadeViewProj[i] = View.CascadeViewProj[cascades - 1];
				continue;
			}
			const float t = static_cast<float>(i + 1) / static_cast<float>(cascades);
			const float logarithmic = nearClip * std::pow(farClip / nearClip, t);
			const float uniform = nearClip + (farClip - nearClip) * t;
			const float split = Lambda * logarithmic + (1.0f - Lambda) * uniform;

			std::array<glm::vec3, 8> corners;
			int index = 0;
			for (float depth : { previous, split })
			{
				const float h = depth * tanHalf;
				const float w = h * camera.AspectRatio;
				for (float sy : { -1.0f, 1.0f })
					for (float sx : { -1.0f, 1.0f })
						corners[index++] = glm::vec3(inverseView * glm::vec4(sx * w, sy * h, -depth, 1.0f));
			}
			glm::vec3 center(0.0f);
			for (const glm::vec3& corner : corners)
				center += corner;
			center /= 8.0f;
			float radius = 0.0f;
			for (const glm::vec3& corner : corners)
				radius = std::max(radius, glm::length(corner - center));
			radius = std::ceil(radius * 16.0f) / 16.0f;

			// Casters behind the cascade (towards the light) are captured by pulling the eye back.
			constexpr float CasterDistance = 60.0f;
			const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			const glm::mat4 lightView = glm::lookAtRH(center - lightDirection * (radius + CasterDistance), center, up);
			glm::mat4 lightProjection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + CasterDistance);

			// Snap the projection to whole texels so shadows do not shimmer when the camera moves.
			const glm::mat4 viewProjection = lightProjection * lightView;
			const float texels = static_cast<float>(settings.CascadeResolution) * 0.5f;
			glm::vec4 origin = viewProjection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			origin *= texels;
			const glm::vec2 offset = (glm::round(glm::vec2(origin)) - glm::vec2(origin)) / texels;
			lightProjection[3][0] += offset.x;
			lightProjection[3][1] += offset.y;

			View.CascadeViewProj[i] = lightProjection * lightView;
			View.CascadeSplits[i] = split;
			previous = split;
		}
		View.CascadeCount = static_cast<float>(cascades);
	}

	void SceneRenderer::Impl::CollectLights(Scene& scene, const RenderCamera& camera, const SceneRendererSettings& settings,
		std::vector<ShadowView>& shadowViews)
	{
		Lights.clear();
		auto& registry = scene.GetRegistry();
		const float time = scene.GetTime();
		View.DirectionalShadowIndex = -1.0f;

		for (auto [handle, light] : registry.view<DirectionalLightComponent>().each())
		{
			if (!light.Enabled || Lights.size() >= GPU::MaxLights)
				continue;
			Entity entity(handle, &scene);
			GPU::Light gpu{};
			gpu.Type = GPU::LightDirectional;
			gpu.Color = light.Color * light.Intensity;
			gpu.Direction = glm::normalize(Math::Forward(scene.GetWorldRotation(entity)));
			gpu.ShadowIndex = -1;
			if (light.CastShadows && View.DirectionalShadowIndex < 0.0f)
			{
				gpu.ShadowIndex = 0;
				View.DirectionalShadowIndex = 0.0f;
				View.ShadowSoftness = std::max(light.ShadowSoftness, 0.05f);
				ComputeCascades(camera, gpu.Direction, settings);
				for (uint32_t c = 0; c < static_cast<uint32_t>(View.CascadeCount); c++)
					shadowViews.push_back({ View.CascadeViewProj[c], CascadeFramebuffers[c], CascadeShadows, c, settings.CascadeResolution });
			}
			Lights.push_back(gpu);
		}
		if (View.DirectionalShadowIndex < 0.0f)
			View.ShadowSoftness = 1.0f;

		struct Candidate
		{
			size_t LightIndex;
			float Distance;
		};
		std::vector<Candidate> spotCandidates, pointCandidates;

		for (auto [handle, light] : registry.view<SpotLightComponent>().each())
		{
			if (!light.Enabled || Lights.size() >= GPU::MaxLights)
				continue;
			Entity entity(handle, &scene);
			GPU::Light gpu{};
			gpu.Type = GPU::LightSpot;
			gpu.Position = scene.GetWorldPosition(entity);
			gpu.Direction = glm::normalize(Math::Forward(scene.GetWorldRotation(entity)));
			gpu.Range = light.Range;
			gpu.Color = light.Color * light.Intensity;
			gpu.CosOuter = std::cos(glm::radians(light.OuterAngle));
			gpu.CosInner = std::cos(glm::radians(std::min(light.InnerAngle, light.OuterAngle)));
			gpu.ShadowIndex = -1;
			if (light.CastShadows)
				spotCandidates.push_back({ Lights.size(), glm::distance(gpu.Position, camera.Position) });
			Lights.push_back(gpu);
		}

		for (auto [handle, light] : registry.view<PointLightComponent>().each())
		{
			if (!light.Enabled || Lights.size() >= GPU::MaxLights)
				continue;
			Entity entity(handle, &scene);
			GPU::Light gpu{};
			gpu.Type = GPU::LightPoint;
			gpu.Position = scene.GetWorldPosition(entity);
			gpu.Range = light.Range;
			gpu.Color = light.Color * light.Intensity * LightUtils::Flicker(time, entity.GetUUID(), light.Flicker);
			gpu.ShadowIndex = -1;
			if (light.CastShadows)
				pointCandidates.push_back({ Lights.size(), glm::distance(gpu.Position, camera.Position) });
			Lights.push_back(gpu);
		}

		// The closest shadowed lights get the limited shadow slots.
		auto byDistance = [](const Candidate& a, const Candidate& b) { return a.Distance < b.Distance; };
		std::sort(spotCandidates.begin(), spotCandidates.end(), byDistance);
		std::sort(pointCandidates.begin(), pointCandidates.end(), byDistance);

		for (uint32_t slot = 0; slot < std::min<size_t>(spotCandidates.size(), GPU::MaxSpotShadows); slot++)
		{
			GPU::Light& light = Lights[spotCandidates[slot].LightIndex];
			const glm::vec3 up = std::abs(light.Direction.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			const float fov = std::min(2.0f * std::acos(std::clamp(light.CosOuter, -1.0f, 1.0f)) + glm::radians(4.0f), glm::radians(170.0f));
			light.ShadowViewProj = glm::perspectiveRH_ZO(fov, 1.0f, ShadowNearPlane, std::max(light.Range, ShadowNearPlane * 2.0f)) *
								   glm::lookAtRH(light.Position, light.Position + light.Direction, up);
			light.ShadowIndex = static_cast<int32_t>(slot);
			shadowViews.push_back({ light.ShadowViewProj, SpotFramebuffers[slot], SpotShadows, slot, settings.SpotShadowResolution });
		}

		// Cube faces in D3D order (+X, -X, +Y, -Y, +Z, -Z); mirrored X to match D3D cube addressing.
		static const glm::vec3 s_FaceDirections[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		static const glm::vec3 s_FaceUps[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
		for (uint32_t slot = 0; slot < std::min<size_t>(pointCandidates.size(), GPU::MaxPointShadows); slot++)
		{
			GPU::Light& light = Lights[pointCandidates[slot].LightIndex];
			light.ShadowIndex = static_cast<int32_t>(slot);
			light.ShadowNear = ShadowNearPlane;
			light.ShadowFar = std::max(light.Range, ShadowNearPlane * 2.0f);
			const glm::mat4 projection = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f)) *
										 glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, light.ShadowNear, light.ShadowFar);
			for (uint32_t face = 0; face < 6; face++)
			{
				const glm::mat4 view = glm::lookAtRH(light.Position, light.Position + s_FaceDirections[face], s_FaceUps[face]);
				const uint32_t layer = slot * 6 + face;
				shadowViews.push_back({ projection * view, PointFramebuffers[layer], PointShadows, layer, settings.PointShadowResolution });
			}
		}
	}

	void SceneRenderer::Impl::DrawFullscreen(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer, nvrhi::IBindingSet* set,
		const GPU::PostConstants& constants, uint32_t width, uint32_t height)
	{
		nvrhi::GraphicsState state;
		state.pipeline = pipeline;
		state.framebuffer = framebuffer;
		state.viewport = FullViewport(width, height);
		state.bindings = { set };
		CommandList->setGraphicsState(state);
		CommandList->setPushConstants(&constants, sizeof(constants));
		CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

	// ------------------------------------------------------------------------------------------
	// SceneRenderer
	// ------------------------------------------------------------------------------------------

	SceneRenderer::SceneRenderer(RenderDevice& device, const SceneRendererSettings& settings)
		: m_Impl(CreateScope<Impl>()), m_Settings(settings)
	{
		Impl& impl = *m_Impl;
		impl.Device = device.GetDevice();
		impl.Cache = CreateScope<GpuResourceCache>(impl.Device);
		impl.CommandList = impl.Device->createCommandList();

		impl.CommandList->open();
		impl.Cache->CreateDefaults(impl.CommandList);
		impl.CommandList->close();
		impl.Device->executeCommandList(impl.CommandList);

		impl.CreateShadersAndLayouts();
		impl.CreatePipelines();
		impl.CreateShadowMaps(m_Settings);
		impl.CreateBRDFLut();
		SetViewportSize(1280, 720);
	}

	SceneRenderer::~SceneRenderer()
	{
		if (m_Impl && m_Impl->Device)
			m_Impl->Device->waitForIdle();
	}

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		width = std::max(width, 1u);
		height = std::max(height, 1u);
		if (width == m_Width && height == m_Height)
			return;
		m_Impl->Device->waitForIdle();
		m_Width = width;
		m_Height = height;
		m_Impl->CreateTargets(width, height, m_Output);
	}

	void SceneRenderer::Render(Scene& scene, const RenderCamera& camera)
	{
		Impl& impl = *m_Impl;
		nvrhi::ICommandList* commandList = impl.CommandList;
		m_Statistics = {};

		// Assets reloaded on disk: drop GPU copies whose CPU source is gone.
		if (impl.AssetGeneration != AssetManager::GetGeneration())
		{
			impl.AssetGeneration = AssetManager::GetGeneration();
			impl.Device->waitForIdle();
			impl.MaterialSets.clear();
			impl.SkinInstances.clear();
			impl.Cache->CollectGarbage();
		}

		impl.CollectGpuTimers();
		commandList->open();
		nvrhi::ITimerQuery* gpuTimer = impl.NextGpuTimer();
		if (gpuTimer)
			commandList->beginTimerQuery(gpuTimer);

		Entity environmentEntity = scene.GetEnvironmentEntity();
		const EnvironmentComponent defaults;
		const EnvironmentComponent& environment = environmentEntity ? environmentEntity.GetComponent<EnvironmentComponent>() : defaults;
		impl.UpdateEnvironment(environmentEntity ? &environment : nullptr);

		impl.FrameIndex++;
		impl.CollectDraws(scene, camera, m_Statistics);
		impl.RunSkinning();
		std::vector<ShadowView> shadowViews;
		impl.CollectLights(scene, camera, m_Settings, shadowViews);
		m_Statistics.Lights = static_cast<uint32_t>(impl.Lights.size());
		m_Statistics.ShadowViews = static_cast<uint32_t>(shadowViews.size());

		// View constants
		GPU::ViewConstants& view = impl.View;
		view.View = camera.View;
		view.Proj = camera.Projection;
		view.ViewProj = camera.Projection * camera.View;
		view.InvProj = glm::inverse(camera.Projection);
		view.InvViewProj = glm::inverse(view.ViewProj);
		view.CameraPosition = camera.Position;
		view.Time = scene.GetTime();
		view.ViewportSize = glm::vec2(static_cast<float>(m_Width), static_cast<float>(m_Height));
		view.InvViewportSize = 1.0f / view.ViewportSize;
		view.AmbientColor = environment.AmbientColor;
		view.EnvironmentIntensity = environment.EnvironmentIntensity;
		view.FogColor = environment.FogColor;
		view.FogDensity = environment.FogDensity;
		view.LightCount = static_cast<uint32_t>(impl.Lights.size());
		view.HasEnvironment = impl.HasEnvironment ? 1u : 0u;
		view.SkyboxIntensity = environment.SkyboxIntensity;
		view.Exposure = environment.Exposure;
		view.PrefilteredMipCount = static_cast<float>(PrefilteredMips);
		view.SSAOEnabled = environment.SSAOEnabled ? 1.0f : 0.0f;
		view.SSAORadius = environment.SSAORadius;
		view.SSAOIntensity = environment.SSAOIntensity;
		view.BloomIntensity = environment.BloomIntensity;
		if (view.DirectionalShadowIndex < 0.0f)
			view.CascadeCount = 1.0f;
		commandList->writeBuffer(impl.ViewBuffer, &view, sizeof(view));
		if (!impl.Lights.empty())
			commandList->writeBuffer(impl.LightBuffer, impl.Lights.data(), impl.Lights.size() * sizeof(GPU::Light));

		// --- Shadows -----------------------------------------------------------------------
		for (const ShadowView& shadow : shadowViews)
		{
			commandList->clearDepthStencilTexture(shadow.Texture, nvrhi::TextureSubresourceSet(0, 1, shadow.Slice, 1), true, 1.0f, false, 0);
			const Frustum frustum(shadow.ViewProj);
			GPU::ShadowConstants constants{};
			constants.ViewProj = shadow.ViewProj;
			for (const DrawItem& draw : impl.Draws)
			{
				if (!draw.CastShadows || draw.Translucent || !frustum.IntersectsBox(draw.Bounds, draw.World))
					continue;
				nvrhi::GraphicsState state;
				state.pipeline = impl.ShadowPipeline;
				state.framebuffer = shadow.Framebuffer;
				state.viewport = FullViewport(shadow.Resolution, shadow.Resolution);
				state.bindings = { impl.ShadowSet };
				state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(draw.VertexBuffer).setSlot(0).setOffset(0) };
				state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(draw.Mesh->IndexBuffer).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
				commandList->setGraphicsState(state);
				constants.WorldRow0 = draw.Constants.WorldRow0;
				constants.WorldRow1 = draw.Constants.WorldRow1;
				constants.WorldRow2 = draw.Constants.WorldRow2;
				commandList->setPushConstants(&constants, sizeof(constants));
				commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(draw.IndexCount).setStartIndexLocation(draw.IndexOffset));
				m_Statistics.DrawCalls++;
			}
		}

		// --- Camera-visible geometry -----------------------------------------------------------
		const Frustum cameraFrustum(view.ViewProj);
		std::vector<const DrawItem*> opaque, translucent;
		for (const DrawItem& draw : impl.Draws)
		{
			if (!cameraFrustum.IntersectsBox(draw.Bounds, draw.World))
			{
				m_Statistics.CulledObjects++;
				continue;
			}
			(draw.Translucent ? translucent : opaque).push_back(&draw);
		}
		std::sort(opaque.begin(), opaque.end(), [](const DrawItem* a, const DrawItem* b) { return a->CameraDistance < b->CameraDistance; });
		std::sort(translucent.begin(), translucent.end(), [](const DrawItem* a, const DrawItem* b) { return a->CameraDistance > b->CameraDistance; });

		nvrhi::IBindingSet* viewSet = impl.GetViewSet();
		auto drawMeshes = [&](const std::vector<const DrawItem*>& items, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer) {
			for (const DrawItem* draw : items)
			{
				nvrhi::GraphicsState state;
				state.pipeline = pipeline;
				state.framebuffer = framebuffer;
				state.viewport = FullViewport(m_Width, m_Height);
				state.bindings = { viewSet, draw->MaterialSet };
				state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(draw->VertexBuffer).setSlot(0).setOffset(0) };
				state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(draw->Mesh->IndexBuffer).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
				commandList->setGraphicsState(state);
				commandList->setPushConstants(&draw->Constants, sizeof(draw->Constants));
				commandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(draw->IndexCount).setStartIndexLocation(draw->IndexOffset));
				m_Statistics.DrawCalls++;
				m_Statistics.Triangles += draw->IndexCount / 3;
			}
		};

		// Prepass: depth + view-space normals.
		commandList->clearDepthStencilTexture(impl.Depth, nvrhi::AllSubresources, true, 1.0f, false, 0);
		commandList->clearTextureFloat(impl.Normals, nvrhi::AllSubresources, nvrhi::Color(0.5f, 0.5f, 1.0f, 0.0f));
		drawMeshes(opaque, impl.PrepassPipeline, impl.PrepassFramebuffer);

		// SSAO
		if (environment.SSAOEnabled)
		{
			GPU::PostConstants constants;
			constants.Params0 = glm::vec4(std::max(environment.SSAORadius, 0.01f), std::max(environment.SSAOIntensity, 0.0f), 0.02f, 0.0f);
			impl.DrawFullscreen(impl.SSAOPipeline, impl.AOFramebuffer, impl.GetPostSet({ impl.Depth }, { impl.Normals }), constants, m_Width, m_Height);
			GPU::PostConstants blur;
			blur.Params0 = glm::vec4(1.0f / static_cast<float>(m_Width), 0.0f, 0.0f, 0.0f);
			impl.DrawFullscreen(impl.SSAOBlurPipeline, impl.AOTempFramebuffer, impl.GetPostSet({ impl.Depth }, { impl.Normals }, { impl.AO }), blur, m_Width, m_Height);
			blur.Params0 = glm::vec4(0.0f, 1.0f / static_cast<float>(m_Height), 0.0f, 0.0f);
			impl.DrawFullscreen(impl.SSAOBlurPipeline, impl.AOFramebuffer, impl.GetPostSet({ impl.Depth }, { impl.Normals }, { impl.AOTemp }), blur, m_Width, m_Height);
		}
		else
		{
			commandList->clearTextureFloat(impl.AO, nvrhi::AllSubresources, nvrhi::Color(1.0f));
		}

		// Lighting
		commandList->clearTextureFloat(impl.HDR, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		drawMeshes(opaque, impl.ForwardPipeline, impl.ForwardFramebuffer);

		nvrhi::ITexture* skyCube = impl.HasEnvironment ? impl.EnvironmentCube.Get() : impl.Cache->GetBlackCubeTexture();
		impl.DrawFullscreen(impl.SkyPipeline, impl.ForwardFramebuffer, impl.GetPostSet({ skyCube }), {}, m_Width, m_Height);

		drawMeshes(translucent, impl.TranslucentPipeline, impl.ForwardFramebuffer);

		// Fire: additive layered billboards, depth-tested against the opaque scene.
		for (auto [handle, flame] : scene.GetRegistry().view<FlameComponent>().each())
		{
			if (!flame.Enabled || flame.Height <= 0.0f || flame.Intensity <= 0.0f)
				continue;
			Entity entity(handle, &scene);
			const glm::vec3 base = scene.GetWorldPosition(entity);
			const glm::vec3 scale(glm::length(glm::vec3(scene.GetWorldTransform(entity)[0])), glm::length(glm::vec3(scene.GetWorldTransform(entity)[1])), 1.0f);
			if (!cameraFrustum.IntersectsSphere(base + glm::vec3(0.0f, flame.Height * 0.5f, 0.0f), flame.Height + flame.Radius))
				continue;
			// Each flame gets its own phase so neighbouring torches never burn in sync.
			const float seed = static_cast<float>(static_cast<uint64_t>(entity.GetUUID()) % 997) * 0.731f;
			GPU::PostConstants constants;
			constants.Params0 = glm::vec4(base, flame.Height * scale.y);
			constants.Params1 = glm::vec4(flame.Radius * scale.x, flame.Intensity, (scene.IsRunning() ? scene.GetTime() : impl.Clock.Elapsed()) * flame.Speed + seed, seed);
			constants.Params2 = glm::vec4(flame.Color, 0.0f);
			nvrhi::GraphicsState state;
			state.pipeline = impl.FlamePipeline;
			state.framebuffer = impl.ForwardFramebuffer;
			state.viewport = FullViewport(m_Width, m_Height);
			state.bindings = { impl.GetPostSet() };
			commandList->setGraphicsState(state);
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(3));
			m_Statistics.DrawCalls++;
		}

		// Smoke volumes (same extinction model as the perception system).
		for (auto [handle, smoke] : scene.GetRegistry().view<SmokeVolumeComponent>().each())
		{
			const glm::vec3 center = scene.GetWorldPosition(Entity(handle, &scene));
			// Temporary smoke billows in over half a second and thins out over the last third of its life.
			float fade = 1.0f;
			if (smoke.Lifetime > 0.0f && scene.IsRunning())
				fade = std::clamp((smoke.Lifetime - smoke.RuntimeAge) / (smoke.Lifetime / 3.0f), 0.0f, 1.0f) *
					   std::clamp(smoke.RuntimeAge / 0.5f, 0.0f, 1.0f);
			GPU::PostConstants constants;
			constants.Params0 = glm::vec4(center, smoke.Radius);
			constants.Params1 = glm::vec4(smoke.Density, fade, 0.0f, 0.0f);
			impl.DrawFullscreen(impl.SmokePipeline, impl.HDRFramebuffer, impl.GetPostSet({ impl.Depth }), constants, m_Width, m_Height);
		}

		// --- Bloom ---------------------------------------------------------------------------
		{
			const uint32_t bloomWidth = std::max(m_Width / 2, 1u), bloomHeight = std::max(m_Height / 2, 1u);
			GPU::PostConstants constants;
			constants.Params0 = glm::vec4(1.0f / static_cast<float>(m_Width), 1.0f / static_cast<float>(m_Height), 0.0f, 0.0f);
			impl.DrawFullscreen(impl.BloomDownPipeline, impl.BloomFramebuffers[0], impl.GetPostSet({ impl.HDR }), constants, bloomWidth, bloomHeight);
			for (uint32_t mip = 1; mip < impl.BloomMips; mip++)
			{
				const uint32_t sourceWidth = std::max(bloomWidth >> (mip - 1), 1u), sourceHeight = std::max(bloomHeight >> (mip - 1), 1u);
				constants.Params0 = glm::vec4(1.0f / static_cast<float>(sourceWidth), 1.0f / static_cast<float>(sourceHeight), 0.0f, 0.0f);
				impl.DrawFullscreen(impl.BloomDownPipeline, impl.BloomFramebuffers[mip],
					impl.GetPostSet({ impl.Bloom, static_cast<int>(mip - 1) }), constants, std::max(bloomWidth >> mip, 1u), std::max(bloomHeight >> mip, 1u));
			}
			for (uint32_t mip = impl.BloomMips - 1; mip > 0; mip--)
			{
				const uint32_t sourceWidth = std::max(bloomWidth >> mip, 1u), sourceHeight = std::max(bloomHeight >> mip, 1u);
				constants.Params0 = glm::vec4(1.0f / static_cast<float>(sourceWidth), 1.0f / static_cast<float>(sourceHeight), 0.0f, 0.0f);
				impl.DrawFullscreen(impl.BloomUpPipeline, impl.BloomFramebuffers[mip - 1], impl.GetPostSet({ impl.Bloom, static_cast<int>(mip) }),
					constants, std::max(bloomWidth >> (mip - 1), 1u), std::max(bloomHeight >> (mip - 1), 1u));
			}
		}

		// --- Tonemap -------------------------------------------------------------------------
		impl.DrawFullscreen(impl.TonemapPipeline, impl.OutputFramebuffer, impl.GetPostSet({ impl.HDR }, { impl.Bloom, 0 }), {}, m_Width, m_Height);

		// --- Debug lines and HUD -------------------------------------------------------------------
		std::vector<GPU::OverlayVertex> hud, world;
		if (m_Settings.DrawOverlay)
		{
			const OverlayList& overlay = scene.GetOverlay();
			// World-space HUD: quads facing the camera (its right and up axes), in metres.
			const glm::mat4 inverseView = glm::inverse(camera.View);
			const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
			const glm::vec3 up = glm::normalize(glm::vec3(inverseView[1]));
			auto worldQuad = [&](const glm::vec3& center, const glm::vec2& min, const glm::vec2& max, const glm::vec4& color) {
				// min/max in metres along right and down (text runs left to right, top to bottom).
				const glm::vec3 a = center + right * min.x - up * min.y, b = center + right * max.x - up * min.y;
				const glm::vec3 c = center + right * max.x - up * max.y, d = center + right * min.x - up * max.y;
				for (const glm::vec3& p : { a, b, c, a, c, d })
					world.push_back({ p, color });
			};
			for (const OverlayWorldRect& rect : overlay.GetWorldRects())
				worldQuad(rect.Center, -rect.Size * 0.5f, rect.Size * 0.5f, rect.Color);
			// The pixel HUD is authored in logical pixels (OverlayScaleFor): larger on tall screens.
			const float uiScale = scene.GetUIScale();
			for (const OverlayRect& rect : overlay.GetRects())
			{
				const glm::vec2 position = rect.Position * uiScale, size = rect.Size * uiScale;
				const glm::vec3 a(position, 0.0f), b(position.x + size.x, position.y, 0.0f);
				const glm::vec3 c(position + size, 0.0f), d(position.x, position.y + size.y, 0.0f);
				for (const glm::vec3& p : { a, b, c, a, c, d })
					hud.push_back({ p, rect.Color });
			}
			std::vector<float> quads(4096 * 16);
			for (const OverlayWorldText& text : overlay.GetWorldTexts())
			{
				// stb_easy_font letters are 7 units tall: scale them to the requested height, centred.
				const float scale = text.Height / 7.0f;
				const float halfWidth = static_cast<float>(stb_easy_font_width(const_cast<char*>(text.Text.c_str()))) * 0.5f;
				const int count = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text.Text.c_str()), nullptr, quads.data(),
					static_cast<int>(quads.size() * sizeof(float)));
				for (int q = 0; q < count; q++)
				{
					const float* first = &quads[static_cast<size_t>(q * 16)];
					const float* third = &quads[static_cast<size_t>(q * 16 + 8)];
					worldQuad(text.Center, glm::vec2(first[0] - halfWidth, first[1] - 3.5f) * scale,
						glm::vec2(third[0] - halfWidth, third[1] - 3.5f) * scale, text.Color);
				}
			}
			for (const OverlayText& text : overlay.GetTexts())
			{
				const int count = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text.Text.c_str()), nullptr, quads.data(),
					static_cast<int>(quads.size() * sizeof(float)));
				for (int q = 0; q < count; q++)
				{
					glm::vec3 corners[4];
					for (int v = 0; v < 4; v++)
					{
						const float* vertex = &quads[static_cast<size_t>((q * 4 + v) * 4)];
						corners[v] = glm::vec3((text.Position + glm::vec2(vertex[0], vertex[1]) * text.Scale) * uiScale, 0.0f);
					}
					for (int index : { 0, 1, 2, 0, 2, 3 })
						hud.push_back({ corners[index], text.Color });
				}
			}
		}
		const std::vector<GPU::OverlayVertex>& lines = m_DebugDraw.GetVertices();
		const size_t overlayCount = hud.size() + world.size() + lines.size();
		if (overlayCount > 0)
		{
			if (overlayCount > impl.OverlayCapacity)
			{
				impl.OverlayCapacity = std::max<size_t>(overlayCount * 2, 4096);
				impl.OverlayBuffer = impl.Device->createBuffer(nvrhi::BufferDesc()
						.setByteSize(impl.OverlayCapacity * sizeof(GPU::OverlayVertex))
						.setIsVertexBuffer(true)
						.setInitialState(nvrhi::ResourceStates::VertexBuffer)
						.setKeepInitialState(true)
						.setDebugName("Overlay"));
			}
			if (!lines.empty())
				commandList->writeBuffer(impl.OverlayBuffer, lines.data(), lines.size() * sizeof(GPU::OverlayVertex), 0);
			if (!world.empty())
				commandList->writeBuffer(impl.OverlayBuffer, world.data(), world.size() * sizeof(GPU::OverlayVertex), lines.size() * sizeof(GPU::OverlayVertex));
			if (!hud.empty())
				commandList->writeBuffer(impl.OverlayBuffer, hud.data(), hud.size() * sizeof(GPU::OverlayVertex),
					(lines.size() + world.size()) * sizeof(GPU::OverlayVertex));

			auto drawOverlay = [&](nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer, size_t first, size_t count) {
				nvrhi::GraphicsState state;
				state.pipeline = pipeline;
				state.framebuffer = framebuffer;
				state.viewport = FullViewport(m_Width, m_Height);
				state.bindings = { impl.GetPostSet() };
				state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(impl.OverlayBuffer).setSlot(0).setOffset(0) };
				commandList->setGraphicsState(state);
				const GPU::PostConstants constants;
				commandList->setPushConstants(&constants, sizeof(constants));
				commandList->draw(nvrhi::DrawArguments().setVertexCount(static_cast<uint32_t>(count)).setStartVertexLocation(static_cast<uint32_t>(first)));
			};
			if (!lines.empty())
				drawOverlay(impl.Overlay3DPipeline, impl.OutputDepthFramebuffer, 0, lines.size());
			if (!world.empty())
				drawOverlay(impl.OverlayWorldPipeline, impl.OutputDepthFramebuffer, lines.size(), world.size());
			if (!hud.empty())
				drawOverlay(impl.Overlay2DPipeline, impl.OutputFramebuffer, lines.size() + world.size(), hud.size());
		}
		m_DebugDraw.Clear();

		if (gpuTimer)
			commandList->endTimerQuery(gpuTimer);
		commandList->close();
		impl.Device->executeCommandList(commandList);
	}

	std::optional<float> SceneRenderer::GetGpuTimeMs() const { return m_Impl->GpuTimeMs; }

	void SceneRenderer::Present(nvrhi::IFramebuffer* target)
	{
		if (!target)
			return;
		Impl& impl = *m_Impl;
		const nvrhi::FramebufferInfoEx& info = target->getFramebufferInfo();
		if (info.colorFormats.empty())
			return;
		const nvrhi::Format format = info.colorFormats[0];
		auto it = impl.BlitPipelines.find(format);
		if (it == impl.BlitPipelines.end())
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = impl.FullscreenVS;
			desc.PS = impl.BlitPS;
			desc.renderState = MakeRenderState(false, false, nvrhi::ComparisonFunc::Always, nvrhi::RasterCullMode::None);
			desc.bindingLayouts = { impl.PostLayout };
			it = impl.BlitPipelines.emplace(format, impl.Device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(format))).first;
		}

		impl.CommandList->open();
		// The view buffer is volatile: it must be written in every command list that reads it.
		impl.CommandList->writeBuffer(impl.ViewBuffer, &impl.View, sizeof(impl.View));
		impl.DrawFullscreen(it->second, target, impl.GetPostSet({ m_Output }), {}, info.width, info.height);
		impl.CommandList->close();
		impl.Device->executeCommandList(impl.CommandList);
	}

	bool SceneRenderer::ReadOutputPixels(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height)
	{
		Impl& impl = *m_Impl;
		nvrhi::TextureDesc desc = m_Output->getDesc();
		desc.isRenderTarget = false;
		desc.initialState = nvrhi::ResourceStates::CopyDest;
		desc.keepInitialState = true;
		nvrhi::StagingTextureHandle staging = impl.Device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
		if (!staging)
			return false;

		impl.CommandList->open();
		impl.CommandList->copyTexture(staging, nvrhi::TextureSlice(), m_Output, nvrhi::TextureSlice());
		impl.CommandList->close();
		impl.Device->executeCommandList(impl.CommandList);
		impl.Device->waitForIdle();

		size_t rowPitch = 0;
		const uint8_t* data = static_cast<const uint8_t*>(impl.Device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		if (!data)
			return false;
		width = desc.width;
		height = desc.height;
		pixels.resize(static_cast<size_t>(width) * height * 4);
		for (uint32_t y = 0; y < height; y++)
			std::memcpy(pixels.data() + static_cast<size_t>(y) * width * 4, data + y * rowPitch, static_cast<size_t>(width) * 4);
		impl.Device->unmapStagingTexture(staging);
		return true;
	}

	bool SceneRenderer::SaveScreenshot(const std::filesystem::path& path)
	{
		std::vector<uint8_t> pixels;
		uint32_t width = 0, height = 0;
		if (!ReadOutputPixels(pixels, width, height))
			return false;
		return TextureImporter::WritePNG(path, width, height, pixels.data(), width * 4);
	}

}
