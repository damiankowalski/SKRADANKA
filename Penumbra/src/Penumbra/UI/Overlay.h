#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Penumbra {

	// The pixel HUD is laid out for a screen this many lines tall and scaled up on taller ones (a
	// Retina Mac's framebuffer has twice the lines of its window): scripts work in these logical
	// pixels (ui.width() / ui.height()), the renderer multiplies them by the scale. Never below 1.
	inline constexpr float OverlayReferenceHeight = 1080.0f;
	inline float OverlayScaleFor(uint32_t viewportHeight)
	{
		return std::max(1.0f, static_cast<float>(viewportHeight) / OverlayReferenceHeight);
	}

	// Immediate-mode HUD primitives emitted by gameplay scripts each frame and drawn
	// on top of the final image by the scene renderer. Coordinates are in logical pixels
	// (see OverlayScaleFor), origin top-left of the viewport.
	struct OverlayRect
	{
		glm::vec2 Position{ 0.0f };
		glm::vec2 Size{ 0.0f };
		glm::vec4 Color{ 1.0f };
	};

	struct OverlayText
	{
		glm::vec2 Position{ 0.0f };
		std::string Text;
		float Scale = 2.0f;
		glm::vec4 Color{ 1.0f };
	};

	// World-space HUD: a rectangle or a line of text hanging at a point in the scene, always facing
	// the camera, sized in metres. Unlike the pixel HUD it is depth tested: walls and characters in
	// front of it hide it (a lockpicking bar on a door the thief's own body can cover).
	struct OverlayWorldRect
	{
		glm::vec3 Center{ 0.0f };
		glm::vec2 Size{ 0.1f };
		glm::vec4 Color{ 1.0f };
	};

	struct OverlayWorldText
	{
		glm::vec3 Center{ 0.0f }; // middle of the line
		std::string Text;
		float Height = 0.05f;     // letter height, metres
		glm::vec4 Color{ 1.0f };
	};

	class OverlayList
	{
	public:
		void AddRect(const OverlayRect& rect) { m_Rects.push_back(rect); }
		void AddText(const OverlayText& text) { m_Texts.push_back(text); }
		void AddWorldRect(const OverlayWorldRect& rect) { m_WorldRects.push_back(rect); }
		void AddWorldText(const OverlayWorldText& text) { m_WorldTexts.push_back(text); }
		void Clear()
		{
			m_Rects.clear();
			m_Texts.clear();
			m_WorldRects.clear();
			m_WorldTexts.clear();
		}

		const std::vector<OverlayRect>& GetRects() const { return m_Rects; }
		const std::vector<OverlayText>& GetTexts() const { return m_Texts; }
		const std::vector<OverlayWorldRect>& GetWorldRects() const { return m_WorldRects; }
		const std::vector<OverlayWorldText>& GetWorldTexts() const { return m_WorldTexts; }

	private:
		std::vector<OverlayRect> m_Rects;
		std::vector<OverlayText> m_Texts;
		std::vector<OverlayWorldRect> m_WorldRects;
		std::vector<OverlayWorldText> m_WorldTexts;
	};

}
