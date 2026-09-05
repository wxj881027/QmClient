/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_SPEEDRUN_TIMER_QM_SPEEDRUN_TIMER_LOGIC_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FEATURES_SPEEDRUN_TIMER_QM_SPEEDRUN_TIMER_LOGIC_H

#include <cstddef>
#include <cstdint>

#include <cstdio>

struct SQmSpeedrunTimerInput
{
	bool m_Enabled = false;
	bool m_HasLocalCharacter = false;
	bool m_RaceStarted = false;
	bool m_CanRequestKill = false;
	int64_t m_CurrentTick = 0;
	int64_t m_StartTick = 0;
	int m_TickSpeed = 0;
	int64_t m_DurationMilliseconds = 0;
	bool m_AutoDisable = false;
};

struct SQmSpeedrunTimerState
{
	bool m_Visible = false;
	bool m_Expired = false;
	int64_t m_RemainingMilliseconds = 0;
};

struct SQmSpeedrunTimerAction
{
	bool m_RequestKill = false;
	bool m_Disable = false;
};

inline int64_t QmSpeedrunTimerConfiguredDurationMilliseconds(int Hours, int Minutes, int Seconds, int Milliseconds)
{
	if(Hours < 0 || Minutes < 0 || Minutes >= 60 || Seconds < 0 || Seconds >= 60 || Milliseconds < 0 || Milliseconds >= 1000)
		return 0;
	return static_cast<int64_t>(Hours) * 60 * 60 * 1000 +
		static_cast<int64_t>(Minutes) * 60 * 1000 +
		static_cast<int64_t>(Seconds) * 1000 + Milliseconds;
}

inline int64_t QmSpeedrunTimerLegacyDurationMilliseconds(int LegacyTime)
{
	if(LegacyTime <= 0)
		return 0;
	const int Minutes = LegacyTime / 100;
	const int Seconds = LegacyTime % 100;
	if(Seconds >= 60)
		return 0;
	return (static_cast<int64_t>(Minutes) * 60 + Seconds) * 1000;
}

inline void QmSpeedrunTimerFormat(int64_t RemainingMilliseconds, char *pBuffer, size_t BufferSize)
{
	if(!pBuffer || BufferSize == 0)
		return;
	if(RemainingMilliseconds < 0)
		RemainingMilliseconds = 0;
	const int64_t Hours = RemainingMilliseconds / (60 * 60 * 1000);
	const int Minutes = static_cast<int>((RemainingMilliseconds / (60 * 1000)) % 60);
	const int Seconds = static_cast<int>((RemainingMilliseconds / 1000) % 60);
	const int Milliseconds = static_cast<int>(RemainingMilliseconds % 1000);
	if(Hours > 0)
		std::snprintf(pBuffer, BufferSize, "%02lld:%02d:%02d.%03d", static_cast<long long>(Hours), Minutes, Seconds, Milliseconds);
	else
		std::snprintf(pBuffer, BufferSize, "%02d:%02d.%03d", Minutes, Seconds, Milliseconds);
	pBuffer[BufferSize - 1] = '\0';
}

class CQmSpeedrunTimerLogic final
{
	SQmSpeedrunTimerState m_State;
	bool m_RaceWasActive = false;
	int64_t m_RaceStartTick = 0;
	int64_t m_ExpiredTick = 0;

public:
	void Reset()
	{
		m_State = {};
		m_RaceWasActive = false;
		m_RaceStartTick = 0;
		m_ExpiredTick = 0;
	}

	SQmSpeedrunTimerAction Update(const SQmSpeedrunTimerInput &Input)
	{
		SQmSpeedrunTimerAction Action;

		if(Input.m_RaceStarted)
		{
			if(!m_RaceWasActive || m_RaceStartTick != Input.m_StartTick)
			{
				m_State = {};
				m_ExpiredTick = 0;
			}
			m_RaceWasActive = true;
			m_RaceStartTick = Input.m_StartTick;
		}
		else
		{
			m_RaceWasActive = false;
		}

		if(m_State.m_Expired)
		{
			const int64_t ExpiredUntil = static_cast<int64_t>(m_ExpiredTick) + static_cast<int64_t>(Input.m_TickSpeed) * 5;
			if(Input.m_HasLocalCharacter && Input.m_TickSpeed > 0 && Input.m_CurrentTick >= m_ExpiredTick && Input.m_CurrentTick < ExpiredUntil)
			{
				m_State.m_Visible = true;
				return Action;
			}
			if(Input.m_TickSpeed <= 0 || Input.m_CurrentTick >= ExpiredUntil)
			{
				m_State = {};
				m_ExpiredTick = 0;
			}
			else
			{
				m_State.m_Visible = false;
				return Action;
			}
		}

		if(!Input.m_Enabled || !Input.m_HasLocalCharacter || !Input.m_RaceStarted || Input.m_TickSpeed <= 0 || Input.m_DurationMilliseconds <= 0)
		{
			m_State = {};
			return Action;
		}

		const int64_t ElapsedTicks = static_cast<int64_t>(Input.m_CurrentTick) - Input.m_StartTick;
		const int64_t DeadlineTicks = Input.m_DurationMilliseconds * Input.m_TickSpeed / 1000;
		const int64_t RemainingTicks = DeadlineTicks - ElapsedTicks;
		if(RemainingTicks <= 0)
		{
			m_State = {true, true, 0};
			m_ExpiredTick = Input.m_CurrentTick;
			Action.m_RequestKill = Input.m_CanRequestKill;
			Action.m_Disable = Input.m_AutoDisable && Action.m_RequestKill;
			return Action;
		}

		m_State = {true, false, RemainingTicks * 1000 / Input.m_TickSpeed};
		return Action;
	}

	const SQmSpeedrunTimerState &State() const { return m_State; }
};

#endif
