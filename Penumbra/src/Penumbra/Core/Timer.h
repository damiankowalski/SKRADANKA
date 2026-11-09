#pragma once

#include <chrono>

namespace Penumbra {

	class Timer
	{
	public:
		Timer() { Reset(); }

		void Reset() { m_Start = std::chrono::steady_clock::now(); }

		float Elapsed() const
		{
			return std::chrono::duration<float>(std::chrono::steady_clock::now() - m_Start).count();
		}

		float ElapsedMillis() const { return Elapsed() * 1000.0f; }

	private:
		std::chrono::steady_clock::time_point m_Start;
	};

}
