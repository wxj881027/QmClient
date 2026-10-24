#ifndef ENGINE_CLIENT_LINK_PROBE_H
#define ENGINE_CLIENT_LINK_PROBE_H

#include <engine/shared/uuid_manager.h>

#include <algorithm>
#include <array>
#include <cstdint>

class CLinkProbeTracker
{
	struct CPending
	{
		CUuid m_Id = {};
		int64_t m_SentTime = -1;
		bool m_Confirmed = false;
	};

	std::array<CPending, 64> m_aPending = {};
	int64_t m_LastFrameTime = -1;
	int64_t m_WindowStart = 0;
	uint64_t m_WindowSamples = 0;
	uint64_t m_WindowUnconfirmed = 0;
	uint64_t m_LastWindowSamples = 0;
	uint64_t m_TotalSamples = 0;
	uint64_t m_TotalUnconfirmed = 0;
	uint64_t m_TotalIgnored = 0;
	float m_UnconfirmedPercent = 0.0f;
	bool m_HasEstimate = false;

public:
	void Reset(int64_t Now)
	{
		m_aPending = {};
		m_LastFrameTime = Now;
		m_WindowStart = Now;
		m_WindowSamples = 0;
		m_WindowUnconfirmed = 0;
		m_LastWindowSamples = 0;
		m_TotalSamples = 0;
		m_TotalUnconfirmed = 0;
		m_TotalIgnored = 0;
		m_UnconfirmedPercent = 0.0f;
		m_HasEstimate = false;
	}

	void OnFrame(int64_t Now, int64_t Frequency)
	{
		// 主线程暂停期间无法判断回复何时到达，取消在途样本并冻结窗口时钟。
		if(m_LastFrameTime >= 0 && Now - m_LastFrameTime > Frequency / 4)
		{
			for(const auto &Pending : m_aPending)
				m_TotalIgnored += Pending.m_SentTime >= 0;
			m_aPending = {};
			m_WindowStart += Now - m_LastFrameTime;
		}
		m_LastFrameTime = Now;
	}

	bool RecordSent(const CUuid &Id, int64_t Now)
	{
		for(auto &Pending : m_aPending)
		{
			if(Pending.m_SentTime < 0)
			{
				Pending = {Id, Now, false};
				return true;
			}
		}
		return false;
	}

	bool Confirm(const CUuid &Id)
	{
		for(auto &Pending : m_aPending)
		{
			if(Pending.m_SentTime >= 0 && Pending.m_Id == Id)
			{
				Pending.m_Confirmed = true;
				return true;
			}
		}
		return false;
	}

	void Update(int64_t Now, int64_t Frequency)
	{
		// 成功和未确认样本都等满两秒再结算，避免窗口只提前计入快速回复。
		for(auto &Pending : m_aPending)
		{
			if(Pending.m_SentTime >= 0 && Now - Pending.m_SentTime >= 2 * Frequency)
			{
				m_WindowSamples++;
				m_TotalSamples++;
				if(!Pending.m_Confirmed)
				{
					m_WindowUnconfirmed++;
					m_TotalUnconfirmed++;
				}
				Pending = {};
			}
		}
		if(Now - m_WindowStart >= 10 * Frequency && m_WindowSamples >= 10)
		{
			const float RawPercent = m_WindowUnconfirmed * 100.0f / m_WindowSamples;
			m_UnconfirmedPercent = m_HasEstimate ? 0.5f * RawPercent + 0.5f * m_UnconfirmedPercent : RawPercent;
			m_HasEstimate = true;
			m_LastWindowSamples = m_WindowSamples;
			m_WindowSamples = 0;
			m_WindowUnconfirmed = 0;
			m_WindowStart = Now;
		}
	}

	bool HasEstimate() const { return m_HasEstimate; }
	float UnconfirmedPercent() const { return m_UnconfirmedPercent; }
	uint64_t WindowSamples() const { return m_HasEstimate ? m_LastWindowSamples : m_WindowSamples; }
	uint64_t TotalSamples() const { return m_TotalSamples; }
	uint64_t TotalUnconfirmed() const { return m_TotalUnconfirmed; }
	uint64_t TotalIgnored() const { return m_TotalIgnored; }

	int Copies(int Limit) const
	{
		int AutoDup = 1;
		if(m_HasEstimate)
		{
			if(m_UnconfirmedPercent > 10.0f)
				AutoDup = 3;
			else if(m_UnconfirmedPercent >= 2.0f)
				AutoDup = 2;
		}
		return std::min(AutoDup, Limit);
	}
};

#endif
