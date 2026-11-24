#include "pnpch.h"
#include "Penumbra/Core/UserSettings.h"

#include "Penumbra/Core/FileSystem.h"

#include <nlohmann/json.hpp>

namespace Penumbra {

	namespace {

		float ClampUiScale(float value) { return std::clamp(value, UserSettings::MinUiScale, UserSettings::MaxUiScale); }

		float ClampRenderScale(float value)
		{
			return value <= 0.0f ? 0.0f : std::clamp(value, UserSettings::MinRenderScale, UserSettings::MaxRenderScale);
		}

	}

	bool UserSettings::Load(const std::filesystem::path& path, UserSettings& settings, std::string& error)
	{
		settings = UserSettings();
		std::error_code exists;
		if (!std::filesystem::exists(path, exists))
			return true;
		const std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
		{
			error = std::format("cannot read {}", path.string());
			return false;
		}
		const nlohmann::json root = nlohmann::json::parse(*text, nullptr, false);
		if (!root.is_object())
		{
			error = std::format("{} is not a JSON object", path.string());
			return false;
		}
		auto number = [&](const char* name, float& value) {
			const auto it = root.find(name);
			if (it == root.end())
				return true;
			if (!it->is_number())
			{
				error = std::format("{}: '{}' must be a number", path.string(), name);
				return false;
			}
			value = it->get<float>();
			return true;
		};
		UserSettings loaded;
		if (!number("UiScale", loaded.UiScale) || !number("RenderScale", loaded.RenderScale))
			return false;
		loaded.UiScale = ClampUiScale(loaded.UiScale);
		loaded.RenderScale = ClampRenderScale(loaded.RenderScale);
		settings = loaded;
		return true;
	}

	bool UserSettings::Save(const std::filesystem::path& path, std::string& error) const
	{
		std::error_code ignored;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), ignored);
		const nlohmann::json root = { { "UiScale", ClampUiScale(UiScale) }, { "RenderScale", ClampRenderScale(RenderScale) } };
		if (!FileSystem::WriteTextFile(path, root.dump(2) + "\n"))
		{
			error = std::format("cannot write {}", path.string());
			return false;
		}
		return true;
	}

	std::filesystem::path UserSettings::GetDefaultPath() { return FileSystem::GetUserDataDirectory() / "Settings.json"; }

	UserSettings UserSettings::LoadDefault()
	{
		UserSettings settings;
		std::string error;
		if (!Load(GetDefaultPath(), settings, error))
			PN_CORE_WARN("Using default user settings: {}", error);
		return settings;
	}

	void UserSettings::SaveDefault() const
	{
		std::string error;
		if (!Save(GetDefaultPath(), error))
			PN_CORE_WARN("User settings not saved: {}", error);
	}

	float UserSettings::ResolveRenderScale(float framebufferScale) const
	{
		const float automatic = framebufferScale > 0.0f ? 1.0f / framebufferScale : 1.0f;
		const float scale = RenderScale > 0.0f ? RenderScale : automatic;
		return std::clamp(scale, MinRenderScale, MaxRenderScale);
	}

}
