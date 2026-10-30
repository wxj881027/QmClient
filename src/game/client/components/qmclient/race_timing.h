#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_TIMING_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RACE_TIMING_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace qm_race_timing
{
	inline std::optional<int> DecodeStartTick(int WarmupTimer) noexcept
	{
		if(WarmupTimer == std::numeric_limits<int>::min())
			return std::nullopt;
		return -WarmupTimer;
	}

	inline int UpdateLastTick(int Previous, bool RaceFlag, int WarmupTimer) noexcept
	{
		if(!RaceFlag)
			return -1;
		// 不可表示的起跑时间不覆盖上一有效状态。
		return DecodeStartTick(WarmupTimer).value_or(Previous);
	}

	inline int64_t TickDifference(int Current, int Start) noexcept
	{
		return static_cast<int64_t>(Current) - Start;
	}

	inline bool IsNewServerStart(int Previous, std::optional<int> Start, int Current, int TickSpeed) noexcept
	{
		return Start && Previous != *Start && TickDifference(Current, *Start) < TickSpeed;
	}

	inline int ElapsedSeconds(int Current, int Start, int TickSpeed) noexcept
	{
		if(Start < 0 || TickSpeed <= 0)
			return 0;
		const int64_t Seconds = TickDifference(Current, Start) / TickSpeed;
		return static_cast<int>(std::clamp<int64_t>(Seconds, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
	}

	inline int64_t GameTimerSeconds(int Current, int RoundStart, int WarmupTimer, int TimeLimit, int TickSpeed, bool Race, bool GameOver) noexcept
	{
		if(TickSpeed <= 0)
			return 0;
		if(TimeLimit && WarmupTimer <= 0)
			return GameOver ? 0 : static_cast<int64_t>(TimeLimit) * 60 - TickDifference(Current, RoundStart) / TickSpeed;
		if(Race)
			return (static_cast<int64_t>(Current) + WarmupTimer) / TickSpeed;
		return TickDifference(Current, RoundStart) / TickSpeed;
	}

	inline bool CanRestartRecording(int Current, int Start, int TickSpeed) noexcept
	{
		return TickDifference(Current, Start) > static_cast<int64_t>(TickSpeed) * 10;
	}

	inline bool PreparationExpired(int Current, int Start, int TickSpeed) noexcept
	{
		return TickDifference(Current, Start) >= static_cast<int64_t>(TickSpeed) * 20;
	}

	inline int RecordingStopTick(int Current, int TickSpeed) noexcept
	{
		// 最后一个可表示tick也要能结束录制，不能保存无法到达的截止值。
		const int64_t Deadline = static_cast<int64_t>(Current) + std::max(TickSpeed, 0);
		return static_cast<int>(std::min<int64_t>(Deadline, std::numeric_limits<int>::max()));
	}
}

#endif
