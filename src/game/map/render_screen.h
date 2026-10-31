#ifndef GAME_MAP_RENDER_SCREEN_H
#define GAME_MAP_RENDER_SCREEN_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace MapRenderScreen
{
	enum class ETileRangeMode
	{
		BUFFERED,
		IMMEDIATE,
	};

	struct STileRange
	{
		int m_X0;
		int m_Y0;
		int m_X1;
		int m_Y1;
	};

	inline std::optional<STileRange> TileRange(float X0, float Y0, float X1, float Y1, float Scale, ETileRangeMode Mode)
	{
		if(!std::isfinite(X0) || !std::isfinite(Y0) || !std::isfinite(X1) || !std::isfinite(Y1) ||
			!std::isfinite(Scale) || !(Scale > 0.0f) || !(X1 > X0) || !(Y1 > Y0) ||
			!std::isfinite(X1 - X0) || !std::isfinite(Y1 - Y0))
			return std::nullopt;
		const bool Immediate = Mode == ETileRangeMode::IMMEDIATE;
		const int Padding = Immediate ? 1 : 0;
		// 保留原 float 除法和两条路径各自的取整方式。
		const float aCoordinates[] = {X0 / Scale, Y0 / Scale, X1 / Scale, Y1 / Scale};
		int aBounds[4];
		constexpr int Max = std::numeric_limits<int>::max();
		for(int i = 0; i < 4; ++i)
		{
			const float Rounded = Immediate ? std::trunc(aCoordinates[i]) : (i < 2 ? std::floor(aCoordinates[i]) : std::ceil(aCoordinates[i]));
			// 排除 INT_MIN，保证边框 abs；同时为即时路径的扩格保留空间。
			if(!std::isfinite(Rounded) || static_cast<double>(Rounded) < -Max + Padding || static_cast<double>(Rounded) > Max - Padding)
				return std::nullopt;
			aBounds[i] = static_cast<int>(Rounded) + (i < 2 ? -Padding : Padding);
		}
		if(static_cast<int64_t>(aBounds[2]) - aBounds[0] > Max || static_cast<int64_t>(aBounds[3]) - aBounds[1] > Max)
			return std::nullopt;
		return STileRange{aBounds[0], aBounds[1], aBounds[2], aBounds[3]};
	}
}

#endif // GAME_MAP_RENDER_SCREEN_H
