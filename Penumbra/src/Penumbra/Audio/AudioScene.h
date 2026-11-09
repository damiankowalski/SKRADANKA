#pragma once

#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <string>

namespace Penumbra {

	// miniaudio-backed audio for a running scene: 3D sources, one-shots and the listener.
	// Sounds with a NoiseRadius also emit noise events that NPCs can hear.
	class AudioScene
	{
	public:
		// deviceEnabled = false creates the engine without an output device (tests, CI),
		// sounds are still decoded and advanced in time.
		AudioScene(Scene& scene, bool deviceEnabled);
		~AudioScene();

		AudioScene(const AudioScene&) = delete;
		AudioScene& operator=(const AudioScene&) = delete;

		void Start();
		void Stop();
		void Update(Timestep ts);
		void OnEntityDestroyed(Entity entity);

		bool Play(Entity entity);
		void StopSource(Entity entity);
		bool IsPlaying(Entity entity) const;
		bool PlayOneShot(const std::string& clip, const glm::vec3& position, float volume = 1.0f, float noiseRadius = 0.0f);

		bool IsInitialized() const;
		size_t GetActiveVoiceCount() const;
		void SetMasterVolume(float volume);

	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

}
