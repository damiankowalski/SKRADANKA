#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Penumbra {

	// Immediate-mode HUD primitives emitted by gameplay scripts each frame and drawn
	// on top of the final image by the scene renderer. Coordinates are in pixels,
	// origin top-left of the viewport.
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

	class OverlayList
	{
	public:
		void AddRect(const OverlayRect& rect) { m_Rects.push_back(rect); }
		void AddText(const OverlayText& text) { m_Texts.push_back(text); }
		void Clear()
		{
			m_Rects.clear();
			m_Texts.clear();
		}

		const std::vector<OverlayRect>& GetRects() const { return m_Rects; }
		const std::vector<OverlayText>& GetTexts() const { return m_Texts; }

	private:
		std::vector<OverlayRect> m_Rects;
		std::vector<OverlayText> m_Texts;
	};

}
