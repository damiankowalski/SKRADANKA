#include "pnpch.h"
#include "Penumbra/ImGui/ImGuiFonts.h"

#include <imgui.h>

extern const unsigned char g_PenumbraFontRoboto[];
extern const unsigned int g_PenumbraFontRobotoSize;
extern const unsigned char g_PenumbraFontCousine[];
extern const unsigned int g_PenumbraFontCousineSize;

namespace Penumbra {

	namespace {

		ImFont* AddEmbedded(ImGuiIO& io, const unsigned char* data, unsigned int size, const char* name)
		{
			ImFontConfig config;
			config.FontDataOwnedByAtlas = false; // static data, lives as long as the program
			std::snprintf(config.Name, sizeof(config.Name), "%s", name);
			ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), ImGuiFonts::BaseSize, &config);
			if (!font)
				PN_CORE_ERROR("The UI font {} could not be loaded; using ImGui's built-in font", name);
			return font;
		}

	}

	bool ImGuiFonts::Load(ImGuiIO& io)
	{
		ImFont* ui = AddEmbedded(io, g_PenumbraFontRoboto, g_PenumbraFontRobotoSize, "Roboto Medium");
		ImFont* monospace = AddEmbedded(io, g_PenumbraFontCousine, g_PenumbraFontCousineSize, "Cousine");
		if (ui)
			io.FontDefault = ui;
		return ui && monospace;
	}

	ImFont* ImGuiFonts::Monospace()
	{
		if (!ImGui::GetCurrentContext())
			return nullptr;
		for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
			if (std::string_view(font->GetDebugName()) == "Cousine")
				return font;
		return nullptr;
	}

}
