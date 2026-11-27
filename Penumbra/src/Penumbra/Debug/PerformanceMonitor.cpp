#include "pnpch.h"
#include "Penumbra/Debug/PerformanceMonitor.h"

#include <nlohmann/json.hpp>

namespace Penumbra {

	namespace {
		void Accumulate(SceneFrameProfile& sum, const SceneFrameProfile& frame, float scale)
		{
			sum.Scripts += frame.Scripts * scale;
			sum.Navigation += frame.Navigation * scale;
			sum.Physics += frame.Physics * scale;
			sum.Contacts += frame.Contacts * scale;
			sum.Stims += frame.Stims * scale;
			sum.Perception += frame.Perception * scale;
			sum.Smoke += frame.Smoke * scale;
			sum.Audio += frame.Audio * scale;
			sum.Ragdolls += frame.Ragdolls * scale;
			sum.Animation += frame.Animation * scale;
			sum.FogOfWar += frame.FogOfWar * scale;
			sum.Destruction += frame.Destruction * scale;
		}
	}

	PerformanceMonitor::PerformanceMonitor(uint32_t logicalCores)
		: m_LogicalCores(std::max(logicalCores, 1u))
	{
	}

	void PerformanceMonitor::AddFrame(const PerformanceSample& sample, double wallSeconds, const Platform::ProcessUsage& usage)
	{
		m_History[m_HistoryNext] = sample.FrameMs;
		m_HistoryNext = (m_HistoryNext + 1) % HistorySize;

		if (!m_Started)
		{
			// The first frame only starts the clocks: CPU time is measured between two readings.
			m_Started = true;
			m_WindowStart = wallSeconds;
			m_CpuAtWindowStart = usage.CpuSeconds;
			m_Summary.ResidentBytes = usage.ResidentBytes;
			m_Summary.PeakResidentBytes = usage.PeakResidentBytes;
			return;
		}

		m_FrameMsMin = m_Frames == 0 ? sample.FrameMs : std::min(m_FrameMsMin, sample.FrameMs);
		m_FrameMsMax = m_Frames == 0 ? sample.FrameMs : std::max(m_FrameMsMax, sample.FrameMs);
		m_Frames++;
		m_FrameMsSum += sample.FrameMs;
		m_UpdateMsSum += sample.UpdateMs;
		m_RenderMsSum += sample.RenderMs;
		if (sample.GpuMs)
		{
			m_GpuMsSum += *sample.GpuMs;
			m_GpuFrames++;
		}
		Accumulate(m_SystemsSum, sample.Systems, 1.0f);

		if (wallSeconds - m_WindowStart >= RefreshInterval)
			Refresh(wallSeconds, usage);
	}

	void PerformanceMonitor::Refresh(double wallSeconds, const Platform::ProcessUsage& usage)
	{
		const double elapsed = wallSeconds - m_WindowStart;
		const float frames = static_cast<float>(m_Frames);
		Summary& summary = m_Summary;
		summary.Frames = m_Frames;
		summary.Fps = elapsed > 0.0 ? static_cast<float>(m_Frames / elapsed) : 0.0f;
		summary.FrameMsAverage = static_cast<float>(m_FrameMsSum) / frames;
		summary.FrameMsMin = m_FrameMsMin;
		summary.FrameMsMax = m_FrameMsMax;
		summary.UpdateMsAverage = static_cast<float>(m_UpdateMsSum) / frames;
		summary.RenderMsAverage = static_cast<float>(m_RenderMsSum) / frames;
		summary.GpuMsAverage = m_GpuFrames > 0 ? std::optional<float>(static_cast<float>(m_GpuMsSum / m_GpuFrames)) : std::nullopt;
		summary.Systems = {};
		Accumulate(summary.Systems, m_SystemsSum, 1.0f / frames);
		const double cpu = elapsed > 0.0 ? std::max(usage.CpuSeconds - m_CpuAtWindowStart, 0.0) / elapsed : 0.0;
		summary.CpuCorePercent = static_cast<float>(cpu * 100.0);
		summary.CpuMachinePercent = summary.CpuCorePercent / static_cast<float>(m_LogicalCores);
		summary.ResidentBytes = usage.ResidentBytes;
		summary.PeakResidentBytes = usage.PeakResidentBytes;
		++summary.Refresh;

		m_Frames = 0;
		m_FrameMsSum = m_UpdateMsSum = m_RenderMsSum = m_GpuMsSum = 0.0;
		m_GpuFrames = 0;
		m_SystemsSum = {};
		m_WindowStart = wallSeconds;
		m_CpuAtWindowStart = usage.CpuSeconds;
	}

	std::string PerformanceLogLine(float gameSeconds, const PerformanceMonitor::Summary& summary, const PerformanceRenderCounts& render,
		const std::optional<glm::vec3>& player)
	{
		auto round2 = [](double value) { return std::round(value * 100.0) / 100.0; };
		nlohmann::json line = {
			{ "t", round2(gameSeconds) },
			{ "frames", summary.Frames },
			{ "fps", round2(summary.Fps) },
			{ "frame_ms", round2(summary.FrameMsAverage) },
			{ "frame_ms_min", round2(summary.FrameMsMin) },
			{ "frame_ms_max", round2(summary.FrameMsMax) },
			{ "update_ms", round2(summary.UpdateMsAverage) },
			{ "render_ms", round2(summary.RenderMsAverage) },
			{ "draws", render.DrawCalls },
			{ "triangles", render.Triangles },
			{ "lights", render.Lights },
			{ "shadow_views", render.ShadowViews },
			{ "left_out_lamps", render.LeftOutLamps },
			{ "culled", render.CulledObjects },
			{ "resident_mb", round2(static_cast<double>(summary.ResidentBytes) / (1024.0 * 1024.0)) },
		};
		line["gpu_ms"] = summary.GpuMsAverage ? nlohmann::json(round2(*summary.GpuMsAverage)) : nlohmann::json(nullptr);
		if (player)
			line["player"] = { round2(player->x), round2(player->y), round2(player->z) };
		return line.dump();
	}

}
