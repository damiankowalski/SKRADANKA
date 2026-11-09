#include "pnpch.h"
#include "Penumbra/Audio/AudioScene.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Project/Project.h"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace Penumbra {

	namespace {
		constexpr float NoiseInterval = 0.25f; // seconds between noise events of a looping source
		constexpr uint32_t SampleRate = 48000;
	}

	struct AudioScene::Impl
	{
		struct Voice
		{
			std::unique_ptr<ma_sound> Sound;
			UUID Owner = UUID::Invalid; // invalid for one-shots
			glm::vec3 Position{ 0.0f };
			float NoiseRadius = 0.0f;
			float NoiseTimer = 0.0f;
		};

		Scene& SceneRef;
		ma_engine Engine{};
		bool Initialized = false;
		bool HasDevice = false;
		std::vector<Voice> Voices;

		explicit Impl(Scene& scene) : SceneRef(scene) {}

		Voice* FindVoice(UUID owner)
		{
			for (Voice& voice : Voices)
			{
				if (voice.Owner == owner)
					return &voice;
			}
			return nullptr;
		}

		std::unique_ptr<ma_sound> LoadSound(const std::string& clip)
		{
			if (!Initialized || clip.empty())
				return nullptr;
			const std::filesystem::path path = Project::ResolveAssetPath(clip);
			auto sound = std::make_unique<ma_sound>();
			const ma_result result = ma_sound_init_from_file_w(&Engine, path.wstring().c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get());
			if (result != MA_SUCCESS)
			{
				PN_CORE_ERROR("Audio: cannot load '{}': {}", clip, ma_result_description(result));
				return nullptr;
			}
			return sound;
		}

		static void Destroy(Voice& voice)
		{
			if (voice.Sound)
			{
				ma_sound_uninit(voice.Sound.get());
				voice.Sound.reset();
			}
		}
	};

	AudioScene::AudioScene(Scene& scene, bool deviceEnabled)
		: m_Impl(CreateScope<Impl>(scene))
	{
		ma_engine_config config = ma_engine_config_init();
		config.sampleRate = SampleRate;
		if (!deviceEnabled)
		{
			config.noDevice = MA_TRUE;
			config.channels = 2;
		}

		ma_result result = ma_engine_init(&config, &m_Impl->Engine);
		if (result != MA_SUCCESS && deviceEnabled)
		{
			// No audio hardware: keep the game running silently.
			PN_CORE_WARN("Audio: no output device ({}), running without sound output", ma_result_description(result));
			config.noDevice = MA_TRUE;
			config.channels = 2;
			result = ma_engine_init(&config, &m_Impl->Engine);
		}
		m_Impl->Initialized = result == MA_SUCCESS;
		m_Impl->HasDevice = m_Impl->Initialized && !config.noDevice;
		if (!m_Impl->Initialized)
			PN_CORE_ERROR("Audio: engine initialization failed: {}", ma_result_description(result));
	}

	AudioScene::~AudioScene()
	{
		Stop();
		if (m_Impl->Initialized)
			ma_engine_uninit(&m_Impl->Engine);
	}

	bool AudioScene::IsInitialized() const { return m_Impl->Initialized; }
	size_t AudioScene::GetActiveVoiceCount() const { return m_Impl->Voices.size(); }

	void AudioScene::SetMasterVolume(float volume)
	{
		if (m_Impl->Initialized)
			ma_engine_set_volume(&m_Impl->Engine, std::max(volume, 0.0f));
	}

	void AudioScene::Start()
	{
		for (auto [handle, source] : m_Impl->SceneRef.GetRegistry().view<AudioSourceComponent>().each())
		{
			if (source.PlayOnStart)
				Play(Entity(handle, &m_Impl->SceneRef));
		}
	}

	void AudioScene::Stop()
	{
		for (Impl::Voice& voice : m_Impl->Voices)
			Impl::Destroy(voice);
		m_Impl->Voices.clear();
	}

	bool AudioScene::Play(Entity entity)
	{
		if (!entity.IsValid() || !entity.HasComponent<AudioSourceComponent>())
			return false;
		const auto& source = entity.GetComponent<AudioSourceComponent>();

		StopSource(entity);
		std::unique_ptr<ma_sound> sound = m_Impl->LoadSound(source.Clip);
		if (!sound)
			return false;

		ma_sound_set_volume(sound.get(), std::max(source.Volume, 0.0f));
		ma_sound_set_pitch(sound.get(), std::max(source.Pitch, 0.01f));
		ma_sound_set_looping(sound.get(), source.Loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_spatialization_enabled(sound.get(), source.Spatial ? MA_TRUE : MA_FALSE);
		ma_sound_set_min_distance(sound.get(), source.MinDistance);
		ma_sound_set_max_distance(sound.get(), source.MaxDistance);
		ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
		const glm::vec3 position = m_Impl->SceneRef.GetWorldPosition(entity);
		ma_sound_set_position(sound.get(), position.x, position.y, position.z);
		ma_sound_start(sound.get());

		Impl::Voice voice;
		voice.Sound = std::move(sound);
		voice.Owner = entity.GetUUID();
		voice.Position = position;
		voice.NoiseRadius = source.NoiseRadius;
		voice.NoiseTimer = 0.0f;
		m_Impl->Voices.push_back(std::move(voice));

		if (source.NoiseRadius > 0.0f)
		{
			if (PerceptionSystem* perception = m_Impl->SceneRef.GetPerceptionSystem())
				perception->EmitNoise(position, source.NoiseRadius, entity.GetUUID());
		}
		return true;
	}

	void AudioScene::StopSource(Entity entity)
	{
		if (!entity.IsValid())
			return;
		const UUID owner = entity.GetUUID();
		for (auto it = m_Impl->Voices.begin(); it != m_Impl->Voices.end();)
		{
			if (it->Owner == owner)
			{
				Impl::Destroy(*it);
				it = m_Impl->Voices.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	bool AudioScene::IsPlaying(Entity entity) const
	{
		if (!entity.IsValid())
			return false;
		Impl::Voice* voice = m_Impl->FindVoice(entity.GetUUID());
		return voice && voice->Sound && ma_sound_is_playing(voice->Sound.get());
	}

	bool AudioScene::PlayOneShot(const std::string& clip, const glm::vec3& position, float volume, float noiseRadius)
	{
		if (noiseRadius > 0.0f)
		{
			if (PerceptionSystem* perception = m_Impl->SceneRef.GetPerceptionSystem())
				perception->EmitNoise(position, noiseRadius, UUID::Invalid);
		}

		std::unique_ptr<ma_sound> sound = m_Impl->LoadSound(clip);
		if (!sound)
			return false;
		ma_sound_set_volume(sound.get(), std::max(volume, 0.0f));
		ma_sound_set_position(sound.get(), position.x, position.y, position.z);
		ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
		ma_sound_start(sound.get());

		Impl::Voice voice;
		voice.Sound = std::move(sound);
		voice.Position = position;
		m_Impl->Voices.push_back(std::move(voice));
		return true;
	}

	void AudioScene::OnEntityDestroyed(Entity entity) { StopSource(entity); }

	void AudioScene::Update(Timestep ts)
	{
		if (!m_Impl->Initialized)
			return;
		Scene& scene = m_Impl->SceneRef;

		// Listener: the active AudioListener, otherwise the primary camera.
		Entity listener;
		for (auto [handle, component] : scene.GetRegistry().view<AudioListenerComponent>().each())
		{
			if (component.Active)
			{
				listener = Entity(handle, &scene);
				break;
			}
		}
		if (!listener)
			listener = scene.GetPrimaryCameraEntity();
		if (listener)
		{
			const glm::vec3 position = scene.GetWorldPosition(listener);
			const glm::vec3 forward = Math::Forward(scene.GetWorldRotation(listener));
			ma_engine_listener_set_position(&m_Impl->Engine, 0, position.x, position.y, position.z);
			ma_engine_listener_set_direction(&m_Impl->Engine, 0, forward.x, forward.y, forward.z);
			ma_engine_listener_set_world_up(&m_Impl->Engine, 0, 0.0f, 1.0f, 0.0f);
		}

		PerceptionSystem* perception = scene.GetPerceptionSystem();
		for (auto it = m_Impl->Voices.begin(); it != m_Impl->Voices.end();)
		{
			Impl::Voice& voice = *it;
			if (voice.Owner.IsValid())
			{
				if (std::optional<Entity> owner = scene.TryGetEntityByUUID(voice.Owner))
				{
					voice.Position = scene.GetWorldPosition(*owner);
					ma_sound_set_position(voice.Sound.get(), voice.Position.x, voice.Position.y, voice.Position.z);
				}
			}

			const bool playing = ma_sound_is_playing(voice.Sound.get()) && !ma_sound_at_end(voice.Sound.get());
			if (playing && voice.NoiseRadius > 0.0f && perception)
			{
				voice.NoiseTimer += ts;
				if (voice.NoiseTimer >= NoiseInterval)
				{
					voice.NoiseTimer = 0.0f;
					perception->EmitNoise(voice.Position, voice.NoiseRadius, voice.Owner);
				}
			}

			if (!playing)
			{
				Impl::Destroy(voice);
				it = m_Impl->Voices.erase(it);
			}
			else
			{
				++it;
			}
		}

		// Without a device nothing pulls audio; advance the mix manually so playback progresses.
		if (!m_Impl->HasDevice)
		{
			const ma_uint64 frames = static_cast<ma_uint64>(ts.GetSeconds() * SampleRate);
			std::array<float, 4096 * 2> scratch{};
			ma_uint64 remaining = frames;
			while (remaining > 0)
			{
				const ma_uint64 chunk = std::min<ma_uint64>(remaining, 4096);
				ma_engine_read_pcm_frames(&m_Impl->Engine, scratch.data(), chunk, nullptr);
				remaining -= chunk;
			}
		}
	}

}
