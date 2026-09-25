// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STUTTER_DIAGNOSTICS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STUTTER_DIAGNOSTICS_H

#include <base/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

inline constexpr double QM_STUTTER_TARGET_FPS = 300.0;
inline constexpr double QM_STUTTER_RECOVERY_MS = 1000.0;
inline constexpr double QM_STUTTER_PERIODIC_FLUSH_MS = 10000.0;

constexpr double QmStutterFrameBudgetMs()
{
	return 1000.0 / QM_STUTTER_TARGET_FPS;
}

inline bool QmStutterFrameBelowTarget(double FrameMs)
{
	return std::isfinite(FrameMs) && FrameMs > QmStutterFrameBudgetMs();
}

enum class EQmStutterFlushReason
{
	NONE,
	PERIODIC,
	RECOVERED,
	DISABLED,
	SHUTDOWN,
};

enum class EQmStutterLimitCause
{
	NONE,
	VSYNC,
	CONFIGURED_LIMIT,
	INACTIVE_LIMIT,
	IDLE_THROTTLE,
};

inline EQmStutterLimitCause QmDetermineStutterLimitCause(bool Vsync, int ConfiguredRenderLimit, int InactiveRenderLimit, bool WindowActive, int IdleRenderLimit = 0)
{
	if(!WindowActive && InactiveRenderLimit > 0 && InactiveRenderLimit < (int)QM_STUTTER_TARGET_FPS)
		return EQmStutterLimitCause::INACTIVE_LIMIT;
	if(ConfiguredRenderLimit > 0 && ConfiguredRenderLimit < (int)QM_STUTTER_TARGET_FPS)
		return EQmStutterLimitCause::CONFIGURED_LIMIT;
	if(IdleRenderLimit > 0 && IdleRenderLimit < (int)QM_STUTTER_TARGET_FPS)
		return EQmStutterLimitCause::IDLE_THROTTLE;
	if(Vsync)
		return EQmStutterLimitCause::VSYNC;
	return EQmStutterLimitCause::NONE;
}

inline const char *QmStutterFlushReasonName(EQmStutterFlushReason Reason)
{
	switch(Reason)
	{
	case EQmStutterFlushReason::PERIODIC: return "periodic";
	case EQmStutterFlushReason::RECOVERED: return "recovered";
	case EQmStutterFlushReason::DISABLED: return "diagnostics_disabled";
	case EQmStutterFlushReason::SHUTDOWN: return "shutdown";
	case EQmStutterFlushReason::NONE: return "none";
	}
	return "none";
}

inline const char *QmStutterLimitCauseName(EQmStutterLimitCause Cause)
{
	switch(Cause)
	{
	case EQmStutterLimitCause::VSYNC: return "vsync";
	case EQmStutterLimitCause::CONFIGURED_LIMIT: return "configured_limit";
	case EQmStutterLimitCause::INACTIVE_LIMIT: return "inactive_limit";
	case EQmStutterLimitCause::IDLE_THROTTLE: return "idle_throttle";
	case EQmStutterLimitCause::NONE: return "none";
	}
	return "none";
}

struct SQmStutterFrameDecision
{
	bool m_Started = false;
	bool m_FlushWindow = false;
	bool m_Ended = false;
	bool m_BelowTarget = false;
	EQmStutterFlushReason m_Reason = EQmStutterFlushReason::NONE;
	uint64_t m_StutterId = 0;
	uint64_t m_Segment = 0;
};

class CQmStutterEpisodeTracker
{
public:
	SQmStutterFrameDecision RecordFrame(uint64_t FrameId, double FrameMs)
	{
		SQmStutterFrameDecision Decision;
		if(!std::isfinite(FrameMs) || FrameMs <= 0.0)
			return Decision;

		Decision.m_BelowTarget = QmStutterFrameBelowTarget(FrameMs);
		if(!m_Active)
		{
			if(!Decision.m_BelowTarget)
				return Decision;
			m_Active = true;
			++m_StutterId;
			m_Segment = 0;
			m_WindowElapsedMs = 0.0;
			m_RecoveryElapsedMs = 0.0;
			m_WindowStartFrame = FrameId;
			Decision.m_Started = true;
		}

		m_WindowElapsedMs += FrameMs;
		if(Decision.m_BelowTarget)
		{
			m_RecoveryElapsedMs = 0.0;
			m_LastBelowTargetFrame = FrameId;
		}
		else
		{
			m_RecoveryElapsedMs += FrameMs;
		}

		Decision.m_StutterId = m_StutterId;
		Decision.m_Segment = m_Segment;
		if(!Decision.m_BelowTarget && m_RecoveryElapsedMs >= QM_STUTTER_RECOVERY_MS)
		{
			Decision.m_FlushWindow = true;
			Decision.m_Ended = true;
			Decision.m_Reason = EQmStutterFlushReason::RECOVERED;
			m_Active = false;
			m_WindowElapsedMs = 0.0;
			m_RecoveryElapsedMs = 0.0;
		}
		else if(m_WindowElapsedMs >= QM_STUTTER_PERIODIC_FLUSH_MS)
		{
			Decision.m_FlushWindow = true;
			Decision.m_Reason = EQmStutterFlushReason::PERIODIC;
			m_WindowElapsedMs = 0.0;
			m_WindowStartFrame = FrameId + 1;
			++m_Segment;
		}
		return Decision;
	}

	SQmStutterFrameDecision Flush(EQmStutterFlushReason Reason)
	{
		SQmStutterFrameDecision Decision;
		if(!m_Active)
			return Decision;
		Decision.m_FlushWindow = true;
		Decision.m_Ended = true;
		Decision.m_Reason = Reason;
		Decision.m_StutterId = m_StutterId;
		Decision.m_Segment = m_Segment;
		m_Active = false;
		m_WindowElapsedMs = 0.0;
		m_RecoveryElapsedMs = 0.0;
		return Decision;
	}

	bool Active() const { return m_Active; }
	uint64_t StutterId() const { return m_StutterId; }
	uint64_t Segment() const { return m_Segment; }
	uint64_t WindowStartFrame() const { return m_WindowStartFrame; }
	uint64_t LastBelowTargetFrame() const { return m_LastBelowTargetFrame; }

private:
	bool m_Active = false;
	uint64_t m_StutterId = 0;
	uint64_t m_Segment = 0;
	uint64_t m_WindowStartFrame = 0;
	uint64_t m_LastBelowTargetFrame = 0;
	double m_WindowElapsedMs = 0.0;
	double m_RecoveryElapsedMs = 0.0;
};

class CQmStutterSampleSeries
{
public:
	void Record(double Value, uint64_t FrameId)
	{
		if(!std::isfinite(Value) || Value < 0.0)
			return;
		m_vSamples.push_back(Value);
		m_Total += Value;
		if(m_vSamples.size() == 1 || Value > m_Max)
		{
			m_Max = Value;
			m_MaxFrame = FrameId;
		}
	}

	double Percentile(double Percent)
	{
		if(m_vSamples.empty())
			return 0.0;
		const size_t Rank = (size_t)std::ceil(std::clamp(Percent, 0.0, 100.0) / 100.0 * m_vSamples.size());
		const size_t Index = std::min(m_vSamples.size() - 1, Rank > 0 ? Rank - 1 : (size_t)0);
		// 样本不暴露时序，累计值与最大帧单独维护；原地选择避免卡顿报告再次复制并完整排序。
		std::nth_element(m_vSamples.begin(), m_vSamples.begin() + Index, m_vSamples.end());
		return m_vSamples[Index];
	}

	void Reset()
	{
		m_vSamples.clear();
		m_Total = 0.0;
		m_Max = 0.0;
		m_MaxFrame = 0;
	}

	bool Empty() const { return m_vSamples.empty(); }
	size_t Count() const { return m_vSamples.size(); }
	double Total() const { return m_Total; }
	double Average() const { return m_vSamples.empty() ? 0.0 : m_Total / m_vSamples.size(); }
	double Max() const { return m_Max; }
	uint64_t MaxFrame() const { return m_MaxFrame; }

private:
	std::vector<double> m_vSamples;
	double m_Total = 0.0;
	double m_Max = 0.0;
	uint64_t m_MaxFrame = 0;
};

#endif
