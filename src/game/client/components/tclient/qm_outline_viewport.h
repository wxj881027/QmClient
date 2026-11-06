#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_QM_OUTLINE_VIEWPORT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_QM_OUTLINE_VIEWPORT_H

#include <game/client/components/qmclient/tile_viewport_range.h>

#include <limits>

struct SQmOutlineTileRange
{
	ivec2 m_Begin;
	ivec2 m_End;
};

inline std::optional<SQmOutlineTileRange> QmCalcOutlineTileRange(vec2 ScreenMin, vec2 ScreenMax, ivec2 Pixels)
{
	const auto Range = QmCalcTileViewportRange(ScreenMin, ScreenMax, Pixels);
	if(!Range)
		return std::nullopt;
	// 描边保留地图外延伸；循环内的八邻域还需要左右各一格可表示。
	const double MinimumBegin = static_cast<double>(std::numeric_limits<int>::min()) + 1;
	const double MaximumEnd = static_cast<double>(std::numeric_limits<int>::max());
	if(Range->m_StartX < MinimumBegin || Range->m_StartY < MinimumBegin ||
		Range->m_EndX > MaximumEnd || Range->m_EndY > MaximumEnd)
		return std::nullopt;
	return SQmOutlineTileRange{
		{static_cast<int>(Range->m_StartX), static_cast<int>(Range->m_StartY)},
		{static_cast<int>(Range->m_EndX), static_cast<int>(Range->m_EndY)}};
}

#endif
