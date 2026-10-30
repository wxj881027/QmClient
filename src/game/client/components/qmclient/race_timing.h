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
}

#endif
