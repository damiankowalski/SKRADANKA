#pragma once

#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/Entity.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace Penumbra::Test {

	// Creates a throw-away project in the temp directory and makes it the active project.
	class TemporaryProject
	{
	public:
		TemporaryProject()
		{
			m_Directory = std::filesystem::temp_directory_path() / "PenumbraTests" / std::to_string(static_cast<uint64_t>(UUID()));
			ProjectConfig config;
			config.Name = "TestProject";
			std::string error;
			const bool created = Project::Create(m_Directory, config, error);
			REQUIRE_MESSAGE(created, error);
		}

		~TemporaryProject()
		{
			Project::Unload();
			std::error_code ignored;
			std::filesystem::remove_all(m_Directory, ignored);
		}

		TemporaryProject(const TemporaryProject&) = delete;
		TemporaryProject& operator=(const TemporaryProject&) = delete;

		const std::filesystem::path& GetDirectory() const { return m_Directory; }
		std::filesystem::path GetAssetDirectory() const { return Project::GetAssetDirectory(); }

		// Writes a file relative to the asset directory.
		void WriteAsset(const std::string& relativePath, std::string_view contents) const
		{
			REQUIRE(FileSystem::WriteTextFile(GetAssetDirectory() / relativePath, contents));
		}

	private:
		std::filesystem::path m_Directory;
	};

	// Writes a mono 16-bit PCM WAV file containing a sine tone.
	void WriteToneWav(const std::filesystem::path& path, float seconds, float frequency = 440.0f, uint32_t sampleRate = 22050);

	// Runs a started scene for the given simulated time at a fixed 60 Hz frame rate.
	void Simulate(Scene& scene, float seconds, float frameTime = 1.0f / 60.0f);

	inline bool Near(float a, float b, float epsilon = 1e-3f) { return std::abs(a - b) <= epsilon; }

	inline bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-3f)
	{
		return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
	}

	// Convenience builders for physics-backed test levels.
	Entity CreateBox(Scene& scene, const std::string& name, const glm::vec3& position, const glm::vec3& halfExtents,
		const glm::vec4& color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), float emissive = 0.0f);

}
