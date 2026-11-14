#include "pnpch.h"
#include "Penumbra/Automation/AutomationHost.h"

#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Core/Platform.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/Scene.h"

namespace Penumbra {

	std::filesystem::path AutomationHost::GetRuntimeExecutable() const
	{
		return FileSystem::GetExecutableDirectory() / Platform::ExecutableName("PenumbraRuntime");
	}

	Ref<Scene> AutomationHost::GetActiveScene()
	{
		Ref<Scene> play = GetPlayScene();
		return play ? play : GetEditScene();
	}

	HeadlessAutomationHost::HeadlessAutomationHost()
		: m_EditScene(CreateRef<Scene>())
	{
		m_EditScene->SetAudioDeviceEnabled(false);
	}

	HeadlessAutomationHost::~HeadlessAutomationHost()
	{
		StopPlay();
		m_Renderer.reset();
		m_RenderDevice.reset();
	}

	void HeadlessAutomationHost::SetEditScene(Ref<Scene> scene)
	{
		StopPlay();
		m_EditScene = std::move(scene);
		m_EditScene->SetAudioDeviceEnabled(m_AudioDeviceEnabled);
	}

	bool HeadlessAutomationHost::StartPlay()
	{
		if (m_PlayScene)
			return false;
		m_PlayScene = Scene::Copy(m_EditScene);
		m_PlayScene->SetAudioDeviceEnabled(m_AudioDeviceEnabled);
		m_PlayScene->OnRuntimeStart();
		return true;
	}

	void HeadlessAutomationHost::StopPlay()
	{
		if (!m_PlayScene)
			return;
		m_PlayScene->OnRuntimeStop();
		m_PlayScene.reset();
	}

	RenderDevice* HeadlessAutomationHost::GetRenderDevice()
	{
		if (!m_RenderDevice && !m_RendererFailed)
		{
			RenderDeviceSpecification spec;
			spec.UseWarp = m_UseWarp;
			if (m_GraphicsApi)
				spec.Api = *m_GraphicsApi;
			m_RenderDevice = RenderDevice::Create(spec);
			m_RendererFailed = m_RenderDevice == nullptr;
		}
		return m_RenderDevice.get();
	}

	SceneRenderer* HeadlessAutomationHost::GetRenderer()
	{
		if (!m_Renderer)
		{
			if (RenderDevice* device = GetRenderDevice())
				m_Renderer = CreateScope<SceneRenderer>(*device);
		}
		return m_Renderer.get();
	}

	std::filesystem::path HeadlessAutomationHost::GetRuntimeExecutable() const
	{
		return m_RuntimeExecutable.empty() ? AutomationHost::GetRuntimeExecutable() : m_RuntimeExecutable;
	}

}
