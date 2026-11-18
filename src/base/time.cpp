/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "time.h"

#include "dbg.h"
#include "detect.h"
#include "str.h"

#include <atomic>
#include <cmath>
#include <iomanip> // std::get_time
#include <sstream> // std::istringstream
#include <utility>

static constexpr int64_t UNCACHED_TICK_TIME = -1;
static std::atomic<int64_t> gs_TickTime{UNCACHED_TICK_TIME};
static std::atomic<int64_t> gs_NextPendingTick{UNCACHED_TICK_TIME - 1};

void set_new_tick()
{
	// 每次刷新使用不同的负值，防止上一帧的取样跨帧发布。
	const int64_t pending_tick = gs_NextPendingTick.fetch_sub(1, std::memory_order_relaxed);
	gs_TickTime.store(pending_tick, std::memory_order_relaxed);
}

static_assert(std::chrono::steady_clock::is_steady, "Compiler does not support steady clocks, it might be out of date.");
static_assert(std::chrono::steady_clock::period::den / std::chrono::steady_clock::period::num >= 1000000000, "Compiler has a bad timer precision and might be out of date.");
static const std::chrono::time_point<std::chrono::steady_clock> GLOBAL_START_TIME = std::chrono::steady_clock::now();

std::chrono::nanoseconds time_get_nanoseconds()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - GLOBAL_START_TIME);
}

int64_t time_get_impl()
{
	return time_get_nanoseconds().count();
}

int64_t time_get()
{
	int64_t tick_time = gs_TickTime.load(std::memory_order_relaxed);
	if(tick_time == UNCACHED_TICK_TIME)
		return time_get_impl();

	while(tick_time < 0)
	{
		const int64_t now = time_get_impl();
		if(gs_TickTime.compare_exchange_strong(tick_time, now, std::memory_order_relaxed))
			return now;
	}
	return tick_time;
}

int64_t time_timestamp()
{
	return time(nullptr);
}

static bool time_localtime(time_t time_data, tm &time_info)
{
#if defined(CONF_FAMILY_WINDOWS)
	// 有效指针配合超范围时间只返回错误码，不触发 CRT 的无效参数处理器。
	return localtime_s(&time_info, &time_data) == 0;
#else
	return localtime_r(&time_data, &time_info) != nullptr;
#endif
}

static bool time_localtime_now(tm &time_info)
{
	const time_t time_data = time(nullptr);
	return time_data != static_cast<time_t>(-1) && time_localtime(time_data, time_info);
}

int time_houroftheday()
{
	tm time_info{};
	return time_localtime_now(time_info) ? time_info.tm_hour : 0;
}

static bool time_iseasterday(const tm &time_info)
{
	// 使用格里高利历算法计算复活节。https://en.wikipedia.org/w/index.php?title=Computus&oldid=890710285#Anonymous_Gregorian_algorithm
	const int64_t Y = static_cast<int64_t>(time_info.tm_year) + 1900;
	if(Y <= 0)
		return false;
	const int64_t a = Y % 19;
	const int64_t b = Y / 100;
	const int64_t c = Y % 100;
	const int64_t d = b / 4;
	const int64_t e = b % 4;
	const int64_t f = (b + 8) / 25;
	const int64_t g = (b - f + 1) / 3;
	const int64_t h = (19 * a + b - d - g + 15) % 30;
	const int64_t i = c / 4;
	const int64_t k = c % 4;
	const int64_t L = (32 + 2 * e + 2 * i - h - k) % 7;
	const int64_t m = (a + 11 * h + 22 * L) / 451;
	const int64_t month = (h + L - 7 * m + 114) / 31;
	const int64_t day = ((h + L - 7 * m + 114) % 31) + 1;

	// 复活节在三月或四月。按本地日期覆盖耶稣受难日至复活节周一，避免夏令时跳时。
	const bool leap_year = Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0);
	const int64_t easter_yday = 31 + 28 + leap_year + (month == 4 ? 31 : 0) + day - 1;
	const int64_t day_offset = time_info.tm_yday - easter_yday;
	return day_offset >= -2 && day_offset <= 1;
}

ETimeSeason time_season()
{
	tm time_info{};
	if(!time_localtime_now(time_info))
		return ETimeSeason::SPRING;

	if((time_info.tm_mon == 11 && time_info.tm_mday == 31) || (time_info.tm_mon == 0 && time_info.tm_mday == 1))
	{
		return ETimeSeason::NEWYEAR;
	}
	else if(time_info.tm_mon == 11 && time_info.tm_mday >= 24 && time_info.tm_mday <= 26)
	{
		return ETimeSeason::XMAS;
	}
	else if((time_info.tm_mon == 9 && time_info.tm_mday == 31) || (time_info.tm_mon == 10 && time_info.tm_mday == 1))
	{
		return ETimeSeason::HALLOWEEN;
	}
	else if(time_iseasterday(time_info))
	{
		return ETimeSeason::EASTER;
	}

	switch(time_info.tm_mon)
	{
	case 11:
	case 0:
	case 1:
		return ETimeSeason::WINTER;
	case 2:
	case 3:
	case 4:
		return ETimeSeason::SPRING;
	case 5:
	case 6:
	case 7:
		return ETimeSeason::SUMMER;
	case 8:
	case 9:
	case 10:
		return ETimeSeason::AUTUMN;
	default:
		dbg_assert_failed("Invalid month: %d", time_info.tm_mon);
	}
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
void str_timestamp(char *buffer, int buffer_size)
{
	str_timestamp_format(buffer, buffer_size, FORMAT_NOSPACE);
}

void str_timestamp_format(char *buffer, int buffer_size, const char *format)
{
	const time_t time_data = time(nullptr);
	if(time_data == static_cast<time_t>(-1))
	{
		if(buffer != nullptr && buffer_size > 0)
			buffer[0] = '\0';
		return;
	}
	str_timestamp_ex(time_data, buffer, buffer_size, format);
}

void str_timestamp_ex(time_t time_data, char *buffer, int buffer_size, const char *format)
{
	if(!std::in_range<int64_t>(time_data))
	{
		if(buffer != nullptr && buffer_size > 0)
			buffer[0] = '\0';
		return;
	}
	(void)str_timestamp_ex_try(static_cast<int64_t>(time_data), buffer, buffer_size, format);
}

bool str_timestamp_ex_try(int64_t timestamp, char *buffer, int buffer_size, const char *format)
{
	if(buffer == nullptr || buffer_size <= 0)
		return false;
	buffer[0] = '\0';
	if(format == nullptr || !std::in_range<time_t>(timestamp))
		return false;
	const time_t time_data = static_cast<time_t>(timestamp);
	tm time_info{};
	if(!time_localtime(time_data, time_info))
		return false;
	if(strftime(buffer, buffer_size, format, &time_info) == 0)
	{
		buffer[0] = '\0';
		return false;
	}
	buffer[buffer_size - 1] = '\0';
	return true;
}

bool timestamp_from_str(const char *string, const char *format, time_t *timestamp)
{
	std::tm tm{};
	tm.tm_isdst = -1; // 根据解析出的日期自动判定夏令时
	std::istringstream ss(string);
	ss >> std::get_time(&tm, format);
	if(ss.fail() || !ss.eof())
		return false;

	time_t result = mktime(&tm);
	if(result < 0)
		return false;

	*timestamp = result;
	return true;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

static int64_t time_rounded_units_or_zero(double rounded)
{
	// 上界是可精确表示的 2^63。NaN、无穷大和超范围结果都回退为零。
	constexpr double LIMIT = -static_cast<double>(INT64_MIN);
	if(rounded >= -LIMIT && rounded < LIMIT)
		return static_cast<int64_t>(rounded);
	return 0;
}

int64_t time_milliseconds_from_seconds(float seconds)
{
	return time_rounded_units_or_zero(std::round(static_cast<double>(seconds) * 1000.0));
}

int64_t time_even_centiseconds_from_seconds(float seconds)
{
	// 服务端 50Hz，一个 tick 是 20ms，厘秒必然是偶数。
	return time_rounded_units_or_zero(std::round(static_cast<double>(seconds) * 50.0) * 2.0);
}

int str_time(int64_t centisecs, int format, char *buffer, int buffer_size)
{
	const int sec = 100;
	const int min = 60 * sec;
	const int hour = 60 * min;
	const int day = 24 * hour;

	if(buffer_size <= 0)
		return -1;

	if(centisecs < 0)
		centisecs = 0;

	buffer[0] = 0;

	switch(format)
	{
	case TIME_DAYS:
		if(centisecs >= day)
			return str_format(buffer, buffer_size, "%" PRId64 "d %02" PRId64 ":%02" PRId64 ":%02" PRId64, centisecs / day,
				(centisecs % day) / hour, (centisecs % hour) / min, (centisecs % min) / sec);
		[[fallthrough]];
	case TIME_HOURS:
		if(centisecs >= hour)
			return str_format(buffer, buffer_size, "%02" PRId64 ":%02" PRId64 ":%02" PRId64, centisecs / hour,
				(centisecs % hour) / min, (centisecs % min) / sec);
		[[fallthrough]];
	case TIME_MINS:
		return str_format(buffer, buffer_size, "%02" PRId64 ":%02" PRId64, centisecs / min,
			(centisecs % min) / sec);
	case TIME_HOURS_CENTISECS:
		if(centisecs >= hour)
			return str_format(buffer, buffer_size, "%02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%02" PRId64, centisecs / hour,
				(centisecs % hour) / min, (centisecs % min) / sec, centisecs % sec);
		[[fallthrough]];
	case TIME_MINS_CENTISECS:
		if(centisecs >= min)
			return str_format(buffer, buffer_size, "%02" PRId64 ":%02" PRId64 ".%02" PRId64, centisecs / min,
				(centisecs % min) / sec, centisecs % sec);
		[[fallthrough]];
	case TIME_SECS_CENTISECS:
		return str_format(buffer, buffer_size, "%02" PRId64 ".%02" PRId64, (centisecs % min) / sec, centisecs % sec);
	}

	return -1;
}

int str_time_float(float secs, int format, char *buffer, int buffer_size)
{
	return str_time(time_even_centiseconds_from_seconds(secs), format, buffer, buffer_size);
}
