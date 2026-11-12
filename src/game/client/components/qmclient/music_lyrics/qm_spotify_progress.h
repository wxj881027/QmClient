#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_PROGRESS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_MUSIC_LYRICS_QM_SPOTIFY_PROGRESS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace QmSpotify
{
	inline int64_t ExtrapolatePositionMs(int64_t PositionMs, int64_t UpdatedTick, int64_t NowTick, int64_t TickFrequency, double PlaybackRate)
	{
		const int64_t Position = std::max<int64_t>(0, PositionMs);
		if(UpdatedTick <= 0 || NowTick <= UpdatedTick || TickFrequency <= 0)
			return Position;
		// 两个时刻此时均为正且已排序，相减不会越过有符号范围。
		const int64_t ElapsedTicks = NowTick - UpdatedTick;
		const double Rate = std::isfinite(PlaybackRate) && PlaybackRate > 0.0 ? PlaybackRate : 1.0;
		const double ElapsedMs = ElapsedTicks * Rate * 1000.0 / static_cast<double>(TickFrequency);
		constexpr int64_t Maximum = std::numeric_limits<int64_t>::max();
		// double 中的 int64 最大值会舍入到 2^63，必须在转换前排除这个上界。
		if(!std::isfinite(ElapsedMs) || ElapsedMs >= static_cast<double>(Maximum))
			return Maximum;
		const int64_t Increment = static_cast<int64_t>(ElapsedMs);
		return Position > Maximum - Increment ? Maximum : Position + Increment;
	}
}

#endif
