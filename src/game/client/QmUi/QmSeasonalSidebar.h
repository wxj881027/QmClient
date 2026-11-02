#ifndef GAME_CLIENT_QMUI_QMSEASONALSIDEBAR_H
#define GAME_CLIENT_QMUI_QMSEASONALSIDEBAR_H

#include <game/client/components/qmclient/qm_sidebar_holiday.h>

#include <cstdint>
#include <limits>

class CUi;
class IGraphics;
class CUIRect;

class CQmSeasonalSidebar
{
	int64_t m_LastMinute = std::numeric_limits<int64_t>::min();
	EQmSidebarHoliday m_Holiday = EQmSidebarHoliday::NONE;

public:
	void Render(CUi *pUi, IGraphics *pGraphics, const CUIRect &Rect, double TimeSec);
};

#endif
