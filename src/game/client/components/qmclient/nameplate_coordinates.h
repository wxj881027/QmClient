#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_COORDINATES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NAMEPLATE_COORDINATES_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

// 保留原float乘法、正负0.5修正和截断步骤；仅将整数出口拓宽并检查值域。
inline std::optional<int64_t> QmNameplateCentitiles(float Tiles)
{
	const float Scaled = Tiles * 100.0f;
	const float Adjusted = Scaled > 0.0f ? Scaled + 0.5f : Scaled - 0.5f;
	const double Wide = Adjusted;
	constexpr double Minimum = static_cast<double>(std::numeric_limits<int64_t>::min());
	if(!std::isfinite(Adjusted) || Wide < Minimum || Wide >= -Minimum)
		return std::nullopt;
	return static_cast<int64_t>(Adjusted);
}

inline std::optional<float> QmNameplateDisplayCoordinate(float Tiles)
{
	const auto Centitiles = QmNameplateCentitiles(Tiles);
	if(!Centitiles)
		return std::nullopt;
	return static_cast<float>(*Centitiles) / 100.0f;
}

// 有符号整数的全域距离最多为UINT64_MAX；先转无符号后相减避免减法和abs溢出。
inline uint64_t QmNameplateCoordinateDistance(int64_t Left, int64_t Right)
{
	return Left >= Right ? static_cast<uint64_t>(Left) - static_cast<uint64_t>(Right) :
			       static_cast<uint64_t>(Right) - static_cast<uint64_t>(Left);
}

#endif
