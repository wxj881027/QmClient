#ifndef BASE_PERF_TIMER_H
#define BASE_PERF_TIMER_H

#include <chrono>

#include <base/system.h>

class CPerfTimer
{
	std::chrono::nanoseconds m_Start;

public:
	CPerfTimer() :
		m_Start(time_get_nanoseconds())
	{
	}

	void Reset()
	{
		m_Start = time_get_nanoseconds();
	}

	double ElapsedMs() const
	{
		return std::chrono::duration<double, std::milli>(time_get_nanoseconds() - m_Start).count();
	}
};

#endif // BASE_PERF_TIMER_H
