#include "ImGui/ImGuiRenderer.h"

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/Log.h"
#include "Penumbra/Renderer/ShaderLibrary.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Penumbra {

	ImGuiRenderer::ImGuiRenderer(nvrhi::IDevice* device)
		: m_Device(device)
	{
		m_CommandList = m_Device->createCommandList();

		const ShaderFormat format = m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN ? ShaderFormat::Spirv : ShaderFormat::Dxil;
		const ShaderBytecode vs = ShaderLibrary::Find("ImGuiVS", format);
		const ShaderBytecode ps = ShaderLibrary::Find("ImGuiPS", format);
		PN_CORE_VERIFY(vs && ps, "ImGui shaders missing from the shader library");
		m_VertexShader = m_Device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Vertex).setDebugName("ImGuiVS"), vs.Data, vs.Size);
		m_PixelShader = m_Device->createShader(nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setDebugName("ImGuiPS"), ps.Data, ps.Size);

		const nvrhi::VertexAttributeDesc attributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert)),
		};
		m_InputLayout = m_Device->createInputLayout(attributes, 3, m_VertexShader);

		nvrhi::BindingLayoutDesc layout;
		layout.registerSpaceIsDescriptorSet = true;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindings = { nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::mat4)), nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(0) };
		m_BindingLayout = m_Device->createBindingLayout(layout);
		m_Sampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "Penumbra NVRHI";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
		ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 16384;
		ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 16384;
	}

	ImGuiRenderer::~ImGuiRenderer() { Shutdown(); }

	void ImGuiRenderer::Shutdown()
	{
		if (!m_Device)
			return;
		m_Device->waitForIdle();
		if (ImGui::GetCurrentContext())
		{
			for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
			{
				if (texture->RefCount == 1)
				{
					texture->SetTexID(ImTextureID_Invalid);
					texture->SetStatus(ImTextureStatus_Destroyed);
				}
			}
		}
		m_Textures.clear();
		m_BindingSets.clear();
		m_Device = nullptr;
	}

	void ImGuiRenderer::UpdateTexture(ImTextureData* texture, nvrhi::ICommandList* commandList)
	{
		if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 2)
		{
			if (const auto it = m_Textures.find(texture); it != m_Textures.end())
			{
				m_BindingSets.erase(it->second.Get());
				m_Textures.erase(it);
			}
			texture->SetTexID(ImTextureID_Invalid);
			texture->SetStatus(ImTextureStatus_Destroyed);
			return;
		}
		if (texture->Status != ImTextureStatus_WantCreate && texture->Status != ImTextureStatus_WantUpdates)
			return;

		// Convert to RGBA8 (the font atlas may be alpha-only).
		std::vector<uint32_t> pixels(static_cast<size_t>(texture->Width) * texture->Height);
		const uint8_t* source = static_cast<const uint8_t*>(texture->GetPixels());
		for (size_t i = 0; i < pixels.size(); i++)
		{
			if (texture->Format == ImTextureFormat_Alpha8)
				pixels[i] = 0x00FFFFFFu | (static_cast<uint32_t>(source[i]) << 24);
			else
				std::memcpy(&pixels[i], source + i * 4, 4);
		}

		nvrhi::TextureHandle& handle = m_Textures[texture];
		if (texture->Status == ImTextureStatus_WantCreate || !handle)
		{
			nvrhi::TextureDesc desc;
			desc.width = static_cast<uint32_t>(texture->Width);
			desc.height = static_cast<uint32_t>(texture->Height);
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.debugName = "ImGuiTexture";
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			if (handle)
				m_BindingSets.erase(handle.Get());
			handle = m_Device->createTexture(desc);
			texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(handle.Get())));
		}
		commandList->writeTexture(handle, 0, 0, pixels.data(), static_cast<size_t>(texture->Width) * 4);
		texture->SetStatus(ImTextureStatus_OK);
	}

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(nvrhi::ITexture* texture)
	{
		if (const auto it = m_BindingSets.find(texture); it != m_BindingSets.end())
			return it->second;
		nvrhi::BindingSetDesc desc;
		desc.bindings = { nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::mat4)), nvrhi::BindingSetItem::Texture_SRV(0, texture),
			nvrhi::BindingSetItem::Sampler(0, m_Sampler) };
		return m_BindingSets.emplace(texture, m_Device->createBindingSet(desc, m_BindingLayout)).first->second;
	}

	nvrhi::IGraphicsPipeline* ImGuiRenderer::GetPipeline(nvrhi::IFramebuffer* framebuffer)
	{
		const nvrhi::Format format = framebuffer->getFramebufferInfo().colorFormats[0];
		if (m_Pipeline && m_PipelineFormat == format)
			return m_Pipeline;

		nvrhi::GraphicsPipelineDesc desc;
		desc.VS = m_VertexShader;
		desc.PS = m_PixelShader;
		desc.inputLayout = m_InputLayout;
		desc.bindingLayouts = { m_BindingLayout };
		desc.renderState.rasterState.setCullNone().setScissorEnable(true);
		desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		desc.renderState.blendState.targets[0]
			.enableBlend()
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
			.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One)
			.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		m_Pipeline = m_Device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
		m_PipelineFormat = format;
		return m_Pipeline;
	}

	void ImGuiRenderer::Render(ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer)
	{
		if (!m_Device || !drawData || !framebuffer)
			return;

		m_CommandList->open();
		if (drawData->Textures)
		{
			for (ImTextureData* texture : *drawData->Textures)
				UpdateTexture(texture, m_CommandList);
		}

		const ImVec2 displaySize = drawData->DisplaySize;
		if (drawData->TotalVtxCount > 0 && displaySize.x > 0.0f && displaySize.y > 0.0f)
		{
			if (static_cast<size_t>(drawData->TotalVtxCount) > m_VertexCapacity)
			{
				m_VertexCapacity = static_cast<size_t>(drawData->TotalVtxCount) + 5000;
				m_VertexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
						.setByteSize(m_VertexCapacity * sizeof(ImDrawVert))
						.setIsVertexBuffer(true)
						.setInitialState(nvrhi::ResourceStates::VertexBuffer)
						.setKeepInitialState(true)
						.setDebugName("ImGuiVertices"));
			}
			if (static_cast<size_t>(drawData->TotalIdxCount) > m_IndexCapacity)
			{
				m_IndexCapacity = static_cast<size_t>(drawData->TotalIdxCount) + 10000;
				m_IndexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
						.setByteSize(m_IndexCapacity * sizeof(ImDrawIdx))
						.setIsIndexBuffer(true)
						.setInitialState(nvrhi::ResourceStates::IndexBuffer)
						.setKeepInitialState(true)
						.setDebugName("ImGuiIndices"));
			}

			std::vector<ImDrawVert> vertices;
			std::vector<ImDrawIdx> indices;
			vertices.reserve(static_cast<size_t>(drawData->TotalVtxCount));
			indices.reserve(static_cast<size_t>(drawData->TotalIdxCount));
			for (const ImDrawList* list : drawData->CmdLists)
			{
				vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
				indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
			}
			m_CommandList->writeBuffer(m_VertexBuffer, vertices.data(), vertices.size() * sizeof(ImDrawVert));
			m_CommandList->writeBuffer(m_IndexBuffer, indices.data(), indices.size() * sizeof(ImDrawIdx));

			const ImVec2 origin = drawData->DisplayPos;
			const glm::mat4 projection = glm::orthoRH_ZO(origin.x, origin.x + displaySize.x, origin.y + displaySize.y, origin.y, 0.0f, 1.0f);
			nvrhi::IGraphicsPipeline* pipeline = GetPipeline(framebuffer);
			const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();

			uint32_t vertexOffset = 0, indexOffset = 0;
			for (const ImDrawList* list : drawData->CmdLists)
			{
				for (const ImDrawCmd& command : list->CmdBuffer)
				{
					if (command.UserCallback)
					{
						if (command.UserCallback != ImDrawCallback_ResetRenderState)
							command.UserCallback(list, &command);
						continue;
					}
					const ImVec2 clipMin(std::max(command.ClipRect.x - origin.x, 0.0f), std::max(command.ClipRect.y - origin.y, 0.0f));
					const ImVec2 clipMax(std::min(command.ClipRect.z - origin.x, static_cast<float>(info.width)),
						std::min(command.ClipRect.w - origin.y, static_cast<float>(info.height)));
					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
						continue;

					auto* texture = reinterpret_cast<nvrhi::ITexture*>(static_cast<uintptr_t>(command.GetTexID()));
					if (!texture)
						continue;

					nvrhi::GraphicsState state;
					state.pipeline = pipeline;
					state.framebuffer = framebuffer;
					state.bindings = { GetBindingSet(texture) };
					state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_VertexBuffer).setSlot(0).setOffset(0) };
					state.indexBuffer = nvrhi::IndexBufferBinding()
											.setBuffer(m_IndexBuffer)
											.setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT)
											.setOffset(0);
					state.viewport.addViewport(nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height)));
					state.viewport.addScissorRect(nvrhi::Rect(static_cast<int>(clipMin.x), static_cast<int>(clipMax.x), static_cast<int>(clipMin.y),
						static_cast<int>(clipMax.y)));
					m_CommandList->setGraphicsState(state);
					m_CommandList->setPushConstants(&projection, sizeof(projection));
					nvrhi::DrawArguments args;
					args.vertexCount = command.ElemCount;
					args.startIndexLocation = indexOffset + command.IdxOffset;
					args.startVertexLocation = vertexOffset + command.VtxOffset;
					m_CommandList->drawIndexed(args);
				}
				vertexOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
				indexOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
			}
		}
		m_CommandList->close();
		m_Device->executeCommandList(m_CommandList);
	}

}
