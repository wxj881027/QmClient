#include "qm_sidebar_holiday.h"

#include "qm_sidebar_calendar_data.h"

#include <algorithm>
#include <cmath>
#include <iterator>

EQmSidebarHoliday QmSidebarHoliday(int Year, int Month, int Day)
{
	if(Year < 1 || Month < 1 || Month > 12 || Day < 1)
		return EQmSidebarHoliday::NONE;
	constexpr int s_aMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	const bool Leap = Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
	if(Day > s_aMonthDays[Month - 1] + (Month == 2 && Leap ? 1 : 0))
		return EQmSidebarHoliday::NONE;
	int Ordinal = Day;
	for(int Index = 0; Index < Month - 1; ++Index)
		Ordinal += s_aMonthDays[Index] + (Index == 1 && Leap ? 1 : 0);
	const int CalendarIndex = Year - QM_SIDEBAR_CALENDAR_FIRST_YEAR;
	if(CalendarIndex >= 0 && CalendarIndex < static_cast<int>(std::size(QM_SIDEBAR_CALENDAR_DATES)))
	{
		const auto &Dates = QM_SIDEBAR_CALENDAR_DATES[CalendarIndex];
		if(Ordinal >= Dates[0] - 1 && Ordinal <= Dates[0] + 6)
			return EQmSidebarHoliday::SPRING_FESTIVAL;
		if(std::abs(Ordinal - Dates[1]) <= 1)
			return EQmSidebarHoliday::QINGMING;
		if(std::abs(Ordinal - Dates[2]) <= 1)
			return EQmSidebarHoliday::DRAGON_BOAT;
		// 中秋与国庆重合时优先显示中秋，结束后继续国庆装饰。
		if(std::abs(Ordinal - Dates[3]) <= 1)
			return EQmSidebarHoliday::MID_AUTUMN;
	}
	if(Month == 5 && Day <= 5)
		return EQmSidebarHoliday::LABOUR_DAY;
	if(Month == 10 && Day <= 7)
		return EQmSidebarHoliday::NATIONAL_DAY;
	if(Month == 10 && Day >= 24)
		return EQmSidebarHoliday::HALLOWEEN;
	if(Month == 12 && Day >= 20 && Day <= 26)
		return EQmSidebarHoliday::CHRISTMAS;
	return EQmSidebarHoliday::NONE;
}

SQmSidebarHolidayParticle QmSidebarHolidayParticle(EQmSidebarHoliday Holiday, int Index, double TimeSec)
{
	if(!std::isfinite(TimeSec))
		TimeSec = 0.0;
	const unsigned Seed = static_cast<unsigned>(std::max(Index, 0)) * 2654435761u + static_cast<unsigned>(Holiday) * 97u;
	const float Phase = static_cast<float>(std::fmod(std::max(TimeSec, 0.0), 12.0) / 12.0);
	const float Y = std::fmod(Phase + (Seed % 1009) / 1009.0f, 1.0f);
	const float Inset = 0.025f + (Seed % 11) * 0.003f;
	return {{Index % 2 == 0 ? Inset : 1.0f - Inset, 0.06f + Y * 0.88f},
		1.2f + (Seed % 4) * 0.45f, 0.12f + 0.18f * std::sin(Y * pi)};
}
