#pragma once

#include "Penumbra/Core/Event.h"
#include "Penumbra/Core/Timestep.h"

#include <string>

namespace Penumbra {

	class Layer
	{
	public:
		explicit Layer(std::string name = "Layer") : m_DebugName(std::move(name)) {}
		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep ts) {}
		// Records rendering for this frame. Layers render in push order; the last one
		// is responsible for producing the final image in the swap chain (if any).
		virtual void OnRender() {}
		virtual void OnEvent(Event& event) {}

		const std::string& GetName() const { return m_DebugName; }

	protected:
		std::string m_DebugName;
	};

}
