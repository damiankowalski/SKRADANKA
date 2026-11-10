#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>

struct ImDrawData;
struct ImTextureData;

namespace Penumbra {

	// Dear ImGui renderer backend on NVRHI. Supports ImGui 1.92 dynamic textures
	// (ImGuiBackendFlags_RendererHasTextures). Textures shown with ImGui::Image are
	// nvrhi::ITexture pointers cast to ImTextureID.
	class ImGuiRenderer
	{
	public:
		explicit ImGuiRenderer(nvrhi::IDevice* device);
		~ImGuiRenderer();

		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

		void Render(ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer);
		void Shutdown();

	private:
		void UpdateTexture(ImTextureData* texture, nvrhi::ICommandList* commandList);
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
		nvrhi::IGraphicsPipeline* GetPipeline(nvrhi::IFramebuffer* framebuffer);

	private:
		nvrhi::IDevice* m_Device;
		nvrhi::CommandListHandle m_CommandList;
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::Format m_PipelineFormat = nvrhi::Format::UNKNOWN;
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
		size_t m_VertexCapacity = 0;
		size_t m_IndexCapacity = 0;
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingSets;
		std::unordered_map<ImTextureData*, nvrhi::TextureHandle> m_Textures;
	};

}
