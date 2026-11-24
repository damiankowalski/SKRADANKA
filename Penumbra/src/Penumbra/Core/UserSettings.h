#pragma once

#include "Penumbra/Core/Base.h"

#include <filesystem>
#include <string>

namespace Penumbra {

	// The user's own choices for the developer tools, shared by the editor and the runtime and kept outside
	// projects (FileSystem::GetUserDataDirectory()/Settings.json). Never part of a scene or a project.
	struct UserSettings
	{
		static constexpr float MinUiScale = 0.5f;
		static constexpr float MaxUiScale = 2.0f;
		static constexpr float MinRenderScale = 0.25f;
		static constexpr float MaxRenderScale = 1.0f;

		// UI size on top of what the screen needs (DPI in the editor, window height in the runtime).
		float UiScale = 1.0f;
		// The 3D picture's resolution as a part of the window's pixels; 0 = automatic (one 3D pixel per
		// window unit: full resolution on a normal screen, half on Retina, the frame rate of a normal screen).
		float RenderScale = 0.0f;

		// Missing file: defaults, true. Unreadable or invalid file: defaults, false and `error`.
		// Values out of range are clamped.
		static bool Load(const std::filesystem::path& path, UserSettings& settings, std::string& error);
		bool Save(const std::filesystem::path& path, std::string& error) const;

		static std::filesystem::path GetDefaultPath();
		// Loads from GetDefaultPath(), logging a problem instead of failing.
		static UserSettings LoadDefault();
		void SaveDefault() const;

		// The render scale in use for a window with `framebufferScale` pixels per window unit.
		float ResolveRenderScale(float framebufferScale) const;
	};

}
