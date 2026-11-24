#pragma once

#include "Penumbra/Core/Base.h"

struct ImFont;
struct ImGuiIO;

namespace Penumbra {

	// The developer tools' fonts, compiled into the executables (cmake/EmbedFile.cmake): Roboto Medium for the
	// UI (Apache License 2.0) and Cousine for numbers, logs and tables (SIL Open Font License 1.1). ImGui 1.92
	// rasterizes them at the size and pixel density they are drawn at, so they stay sharp at every UI size and on
	// Retina.
	class ImGuiFonts
	{
	public:
		static constexpr float BaseSize = 15.0f; // window units at UI size 1

		// Adds both fonts to io.Fonts; the UI font becomes the default. False (with a log) if a font is rejected,
		// in which case ImGui keeps its built-in font.
		static bool Load(ImGuiIO& io);

		// The monospace font of the current ImGui context (null before Load): ImGui::PushFont(Monospace(), 0.0f).
		static ImFont* Monospace();
	};

}
