#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestHelpers.h"

#include "Penumbra/Core/Log.h"
#include "Penumbra/Scene/Scene.h"

namespace Penumbra::Test {

	void WriteToneWav(const std::filesystem::path& path, float seconds, float frequency, uint32_t sampleRate)
	{
		const uint32_t sampleCount = static_cast<uint32_t>(seconds * static_cast<float>(sampleRate));
		std::vector<uint8_t> data;
		auto write32 = [&](uint32_t value) {
			for (int i = 0; i < 4; i++)
				data.push_back(static_cast<uint8_t>(value >> (8 * i)));
		};
		auto write16 = [&](uint16_t value) {
			data.push_back(static_cast<uint8_t>(value));
			data.push_back(static_cast<uint8_t>(value >> 8));
		};
		auto writeTag = [&](const char* tag) { data.insert(data.end(), tag, tag + 4); };

		writeTag("RIFF");
		write32(36 + sampleCount * 2);
		writeTag("WAVE");
		writeTag("fmt ");
		write32(16);
		write16(1); // PCM
		write16(1); // mono
		write32(sampleRate);
		write32(sampleRate * 2);
		write16(2);
		write16(16);
		writeTag("data");
		write32(sampleCount * 2);
		for (uint32_t i = 0; i < sampleCount; i++)
		{
			const float sample = std::sin(6.2831853f * frequency * static_cast<float>(i) / static_cast<float>(sampleRate)) * 0.25f;
			write16(static_cast<uint16_t>(static_cast<int16_t>(sample * 32767.0f)));
		}
		REQUIRE(FileSystem::WriteBinaryFile(path, data.data(), data.size()));
	}

	void Simulate(Scene& scene, float seconds, float frameTime)
	{
		const int frames = static_cast<int>(std::ceil(seconds / frameTime));
		for (int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(Timestep(frameTime));
	}

	Entity CreateBox(Scene& scene, const std::string& name, const glm::vec3& position, const glm::vec3& halfExtents, const glm::vec4& color,
		float emissive)
	{
		Entity entity = scene.CreateEntity(name);
		auto& transform = entity.GetComponent<TransformComponent>();
		transform.Translation = position;
		transform.Scale = halfExtents * 2.0f;
		entity.AddComponent<MeshComponent>().Mesh = "builtin:cube";
		auto& material = entity.AddComponent<MaterialComponent>();
		material.BaseColor = color;
		material.EmissiveIntensity = emissive;
		entity.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f);
		return entity;
	}

}

int main(int argc, char** argv)
{
	// Keep test output readable: only warnings and errors from the engine (--trace shows everything).
	bool trace = false;
	for (int i = 1; i < argc; i++)
		trace |= std::string_view(argv[i]) == "--trace";
	Penumbra::Log::SetMinimumLevel(trace ? Penumbra::LogLevel::Trace : Penumbra::LogLevel::Warn);

	doctest::Context context;
	context.applyCommandLine(argc, argv);
	const int result = context.run();
	Penumbra::Log::Shutdown();
	return result;
}
