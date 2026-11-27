#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/Platform.h"
#include "Penumbra/Scene/Scene.h"

#include <array>
#include <optional>
#include <string>

namespace Penumbra {

	// One frame as the runtime measured it.
	struct PerformanceSample
	{
		float FrameMs = 0.0f;  // wall time since the previous frame
		float UpdateMs = 0.0f; // simulation (Scene::OnUpdateRuntime)
		float RenderMs = 0.0f; // CPU side of rendering: building and submitting the frame
		std::optional<float> GpuMs;
		SceneFrameProfile Systems;
	};

	// Frame statistics for the runtime's performance overlay (F3). Frames are added as they come
	// with the wall clock and the process's CPU time; every RefreshInterval the summary is
	// recomputed over the frames since the last refresh, so the numbers hold still long enough to
	// read. Pure bookkeeping: the caller measures (Platform::GetProcessUsage, timers, GPU queries).
	class PerformanceMonitor
	{
	public:
		static constexpr size_t HistorySize = 240; // frame times kept for the graph
		static constexpr double RefreshInterval = 0.5;

		struct Summary
		{
			float Fps = 0.0f;
			float FrameMsAverage = 0.0f;
			float FrameMsMin = 0.0f;
			float FrameMsMax = 0.0f;
			float UpdateMsAverage = 0.0f;
			float RenderMsAverage = 0.0f;
			std::optional<float> GpuMsAverage;
			SceneFrameProfile Systems; // averages
			float CpuCorePercent = 0.0f;   // 100 = one core fully busy
			float CpuMachinePercent = 0.0f; // share of every logical core
			uint64_t ResidentBytes = 0;
			uint64_t PeakResidentBytes = 0;
			uint32_t Frames = 0;  // frames the summary covers
			uint64_t Refresh = 0; // counts the refreshes: a new number, a new summary
		};

		explicit PerformanceMonitor(uint32_t logicalCores = Platform::GetLogicalCoreCount());

		// wallSeconds: a steady clock (any origin) read at the end of the frame.
		void AddFrame(const PerformanceSample& sample, double wallSeconds, const Platform::ProcessUsage& usage);

		const Summary& GetSummary() const { return m_Summary; }
		// Frame times in milliseconds, oldest first, for a graph (HistorySize entries, zeros until filled).
		const std::array<float, HistorySize>& GetFrameHistory() const { return m_History; }
		size_t GetHistoryOffset() const { return m_HistoryNext; } // index of the oldest entry

	private:
		void Refresh(double wallSeconds, const Platform::ProcessUsage& usage);

	private:
		uint32_t m_LogicalCores = 1;
		std::array<float, HistorySize> m_History{};
		size_t m_HistoryNext = 0;

		// Accumulated since the last refresh.
		uint32_t m_Frames = 0;
		double m_FrameMsSum = 0.0, m_UpdateMsSum = 0.0, m_RenderMsSum = 0.0, m_GpuMsSum = 0.0;
		uint32_t m_GpuFrames = 0;
		float m_FrameMsMin = 0.0f, m_FrameMsMax = 0.0f;
		SceneFrameProfile m_SystemsSum;

		bool m_Started = false;
		double m_WindowStart = 0.0;
		double m_CpuAtWindowStart = 0.0;
		Summary m_Summary;
	};

	// What the renderer did in the last frame (SceneRendererStatistics), for the frame-statistics log.
	struct PerformanceRenderCounts
	{
		uint32_t DrawCalls = 0;
		uint32_t Triangles = 0;
		uint32_t Lights = 0;
		uint32_t ShadowViews = 0;
		uint32_t LeftOutLamps = 0;
		uint32_t CulledObjects = 0;
	};

	// One JSON line of the frame-statistics log (PenumbraRuntime --perf-out, Tools/Benchmark.py): the game's
	// clock, the last summary (averages over its RefreshInterval), the renderer's counts and the player's
	// position when the scene has one.
	std::string PerformanceLogLine(float gameSeconds, const PerformanceMonitor::Summary& summary, const PerformanceRenderCounts& render,
		const std::optional<glm::vec3>& player);

}
