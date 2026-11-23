// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_PERF_WINDOWS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_PERF_WINDOWS_H

#include <base/math.h>
#include <base/system.h>

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <vector>

struct SQmSettingsPerfWindowSummary
{
	char m_aOperation[64] = "";
	char m_aContext[16] = "";
	char m_aPage[64] = "";
	char m_aTab[32] = "";
	int m_SampleFrames = 0;
	float m_SampleSeconds = 0.0f;
	float m_FpsAvg = 0.0f;
	float m_FpsMin = 0.0f;
	float m_FpsOnePctLow = 0.0f;
	float m_FpsMax = 0.0f;
	float m_FrameMsAvg = 0.0f;
	float m_FrameMsP95 = 0.0f;
	float m_FrameMsP99 = 0.0f;
	float m_FrameMsMax = 0.0f;
	float m_MenuMsMax = 0.0f;
	uint64_t m_WindowStartFrame = 0;
	uint64_t m_WindowEndFrame = 0;
	bool m_CapLimited = false;
};

struct SQmSettingsPerfWindowFrameResult
{
	bool m_ShouldFlush = false;
	SQmSettingsPerfWindowSummary m_Summary;
};

class CQmSettingsPerfWindowTracker
{
public:
	SQmSettingsPerfWindowFrameResult StartFixedFrameWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab, int MaxFrames, bool CapLimited, uint64_t WindowStartFrame = 0)
	{
		const SQmSettingsPerfWindowFrameResult Interrupted = FlushInterruptedWindow();
		Reset();
		m_Active = true;
		m_ScrollWindow = false;
		m_MaxFrames = maximum(1, MaxFrames);
		m_Summary.m_WindowStartFrame = WindowStartFrame;
		m_Summary.m_WindowEndFrame = WindowStartFrame;
		m_Summary.m_CapLimited = CapLimited;
		str_copy(m_Summary.m_aOperation, pOperation != nullptr ? pOperation : "", sizeof(m_Summary.m_aOperation));
		str_copy(m_Summary.m_aContext, pContext != nullptr ? pContext : "", sizeof(m_Summary.m_aContext));
		str_copy(m_Summary.m_aPage, pPage != nullptr ? pPage : "", sizeof(m_Summary.m_aPage));
		str_copy(m_Summary.m_aTab, pTab != nullptr ? pTab : "", sizeof(m_Summary.m_aTab));
		return Interrupted;
	}

	SQmSettingsPerfWindowFrameResult StartScrollWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab, float IdleTimeoutSeconds, bool CapLimited, uint64_t WindowStartFrame = 0)
	{
		const SQmSettingsPerfWindowFrameResult Interrupted = FlushInterruptedWindow();
		Reset();
		m_Active = true;
		m_ScrollWindow = true;
		m_IdleTimeoutSeconds = maximum(0.0f, IdleTimeoutSeconds);
		m_Summary.m_WindowStartFrame = WindowStartFrame;
		m_Summary.m_WindowEndFrame = WindowStartFrame;
		m_Summary.m_CapLimited = CapLimited;
		str_copy(m_Summary.m_aOperation, pOperation != nullptr ? pOperation : "", sizeof(m_Summary.m_aOperation));
		str_copy(m_Summary.m_aContext, pContext != nullptr ? pContext : "", sizeof(m_Summary.m_aContext));
		str_copy(m_Summary.m_aPage, pPage != nullptr ? pPage : "", sizeof(m_Summary.m_aPage));
		str_copy(m_Summary.m_aTab, pTab != nullptr ? pTab : "", sizeof(m_Summary.m_aTab));
		return Interrupted;
	}

	SQmSettingsPerfWindowFrameResult EnsureScrollWindow(const char *pOperation, const char *pContext, const char *pPage, const char *pTab, float IdleTimeoutSeconds, bool CapLimited, uint64_t WindowStartFrame = 0)
	{
		if(m_Active && m_ScrollWindow && str_comp(m_Summary.m_aOperation, pOperation != nullptr ? pOperation : "") == 0)
			return {};
		return StartScrollWindow(pOperation, pContext, pPage, pTab, IdleTimeoutSeconds, CapLimited, WindowStartFrame);
	}

	bool HasActiveWindow() const { return m_Active; }
	const char *ActiveOperation() const { return m_Active ? m_Summary.m_aOperation : "none"; }
	const char *ActivePage() const { return m_Active ? m_Summary.m_aPage : ""; }

	SQmSettingsPerfWindowFrameResult RecordFrame(float RenderFrameTimeSeconds, double MenuDurationMs, bool ScrollInputActive, uint64_t FrameId = 0)
	{
		SQmSettingsPerfWindowFrameResult Result;
		if(!m_Active)
			return Result;

		const bool ValidFrameTime = std::isfinite(RenderFrameTimeSeconds) && RenderFrameTimeSeconds > 0.0f && RenderFrameTimeSeconds < 1.0f;
		if(ValidFrameTime)
		{
			const float FrameMs = RenderFrameTimeSeconds * 1000.0f;
			if(!TryAppendFrameMs(FrameMs))
				return {m_Summary.m_SampleFrames > 0, FinishActiveWindow()};

			const float Fps = 1.0f / RenderFrameTimeSeconds;
			m_Summary.m_SampleFrames++;
			m_Summary.m_SampleSeconds += RenderFrameTimeSeconds;
			if(m_Summary.m_WindowStartFrame == 0)
				m_Summary.m_WindowStartFrame = FrameId;
			m_Summary.m_WindowEndFrame = FrameId != 0 ? FrameId : m_Summary.m_WindowEndFrame;
			m_Summary.m_FrameMsMax = maximum(m_Summary.m_FrameMsMax, FrameMs);
			m_Summary.m_MenuMsMax = maximum(m_Summary.m_MenuMsMax, (float)MenuDurationMs);
			m_Summary.m_FpsMin = m_Summary.m_FpsMin <= 0.0f ? Fps : minimum(m_Summary.m_FpsMin, Fps);
			m_Summary.m_FpsMax = maximum(m_Summary.m_FpsMax, Fps);
		}

		if(m_ScrollWindow)
		{
			if(ScrollInputActive)
				m_IdleSeconds = 0.0f;
			else if(ValidFrameTime)
				m_IdleSeconds += RenderFrameTimeSeconds;
			if(m_Summary.m_SampleFrames > 0 && m_IdleSeconds >= m_IdleTimeoutSeconds)
			{
				Result.m_ShouldFlush = true;
				Result.m_Summary = FinishActiveWindow();
			}
		}
		else if(m_Summary.m_SampleFrames >= m_MaxFrames)
		{
			Result.m_ShouldFlush = true;
			Result.m_Summary = FinishActiveWindow();
		}

		return Result;
	}

	SQmSettingsPerfWindowSummary FinishActiveWindow()
	{
		FinalizeSummary();
		const SQmSettingsPerfWindowSummary Summary = m_Summary;
		Reset();
		return Summary;
	}

private:
	bool TryAppendFrameMs(float FrameMs)
	{
		try
		{
			m_vFrameMs.push_back(FrameMs);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}

	SQmSettingsPerfWindowFrameResult FlushInterruptedWindow()
	{
		SQmSettingsPerfWindowFrameResult Result;
		if(m_Active && m_Summary.m_SampleFrames > 0)
		{
			Result.m_ShouldFlush = true;
			Result.m_Summary = FinishActiveWindow();
		}
		return Result;
	}

	float PercentileOfSortedFrames(int Percent) const
	{
		const int Index = minimum((int)m_vFrameMs.size() - 1, maximum(0, (int)std::ceil((Percent / 100.0f) * m_vFrameMs.size()) - 1));
		return m_vFrameMs[Index];
	}

	float OnePercentLowFpsOfSortedFrames() const
	{
		const int SlowFrameCount = minimum((int)m_vFrameMs.size(), maximum(1, (int)std::ceil((float)m_vFrameMs.size() * 0.01f)));
		float SlowFrameMsTotal = 0.0f;
		for(int i = 0; i < SlowFrameCount; ++i)
			SlowFrameMsTotal += m_vFrameMs[m_vFrameMs.size() - 1 - i];
		const float SlowFrameMsAvg = SlowFrameMsTotal / (float)SlowFrameCount;
		return SlowFrameMsAvg > 0.0f ? 1000.0f / SlowFrameMsAvg : 0.0f;
	}

	void FinalizeSummary()
	{
		if(m_Summary.m_SampleFrames <= 0)
			return;
		std::sort(m_vFrameMs.begin(), m_vFrameMs.end());
		m_Summary.m_FpsAvg = m_Summary.m_SampleSeconds > 0.0f ? (float)m_Summary.m_SampleFrames / m_Summary.m_SampleSeconds : 0.0f;
		m_Summary.m_FrameMsAvg = m_Summary.m_SampleSeconds > 0.0f ? (m_Summary.m_SampleSeconds * 1000.0f) / (float)m_Summary.m_SampleFrames : 0.0f;
		m_Summary.m_FrameMsP95 = PercentileOfSortedFrames(95);
		m_Summary.m_FrameMsP99 = PercentileOfSortedFrames(99);
		m_Summary.m_FpsOnePctLow = OnePercentLowFpsOfSortedFrames();
	}

	void Reset()
	{
		m_Active = false;
		m_ScrollWindow = false;
		m_MaxFrames = 0;
		m_IdleTimeoutSeconds = 0.0f;
		m_IdleSeconds = 0.0f;
		m_Summary = SQmSettingsPerfWindowSummary{};
		m_vFrameMs.clear();
	}

	bool m_Active = false;
	bool m_ScrollWindow = false;
	int m_MaxFrames = 0;
	float m_IdleTimeoutSeconds = 0.0f;
	float m_IdleSeconds = 0.0f;
	SQmSettingsPerfWindowSummary m_Summary;
	std::vector<float> m_vFrameMs;
};

#endif
