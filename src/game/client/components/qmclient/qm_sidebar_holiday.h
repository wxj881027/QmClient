#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SIDEBAR_HOLIDAY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SIDEBAR_HOLIDAY_H

#include <base/vmath.h>

enum class EQmSidebarHoliday
{
	NONE,
	SPRING_FESTIVAL,
	QINGMING,
	LABOUR_DAY,
	DRAGON_BOAT,
	MID_AUTUMN,
	NATIONAL_DAY,
	HALLOWEEN,
	CHRISTMAS,
};

EQmSidebarHoliday QmSidebarHoliday(int Year, int Month, int Day);

struct SQmSidebarHolidayParticle
{
	vec2 m_Position;
	float m_Size;
	float m_Alpha;
};

constexpr int QM_SIDEBAR_HOLIDAY_PARTICLES = 18;
SQmSidebarHolidayParticle QmSidebarHolidayParticle(EQmSidebarHoliday Holiday, int Index, double TimeSec);

#endif
