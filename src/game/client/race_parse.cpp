#include "race.h"

#include <base/system.h>

#include <iterator>
#include <limits>
#include <string_view>

namespace
{
	bool IsTimeDigit(char Character)
	{
		return Character >= '0' && Character <= '9';
	}

	bool ReadTimeNumber(const char *&pStr, int Limit, int &Number)
	{
		if(!IsTimeDigit(*pStr))
			return false;
		Number = 0;
		do
		{
			const int Digit = *pStr - '0';
			if(Number > Limit / 10 || (Number == Limit / 10 && Digit > Limit % 10))
				return false;
			Number = Number * 10 + Digit;
			++pStr;
		} while(IsTimeDigit(*pStr));
		return true;
	}
}

int CRaceHelper::TimeFromSecondsStr(const char *pStr)
{
	while(*pStr == ' ') // skip leading spaces
		pStr++;
	int Seconds;
	if(!ReadTimeNumber(pStr, std::numeric_limits<int>::max() / 1000, Seconds))
		return -1;
	int Time = Seconds * 1000;
	if(*pStr == '.' || *pStr == ',')
	{
		pStr++;
		static const int s_aMult[3] = {100, 10, 1};
		for(size_t i = 0; i < std::size(s_aMult) && IsTimeDigit(pStr[i]); i++)
		{
			const int Milliseconds = (pStr[i] - '0') * s_aMult[i];
			if(Time > std::numeric_limits<int>::max() - Milliseconds)
				return -1;
			Time += Milliseconds;
		}
	}
	return Time;
}

int CRaceHelper::TimeFromDemoFilename(const char *pFilename, const char *pMap, const char *pPlayerName)
{
	const char *pExtension = str_endswith(pFilename, ".demo");
	const char *pAfterMap = str_startswith(pFilename, pMap);
	if(!pExtension || !pAfterMap || pAfterMap >= pExtension || *pAfterMap != '_')
		return -1;

	const char *pTime = pAfterMap + 1;
	const char *pTimeEnd = pTime;
	while(pTimeEnd < pExtension && (IsTimeDigit(*pTimeEnd) || *pTimeEnd == ' ' || *pTimeEnd == '.' || *pTimeEnd == ','))
		++pTimeEnd;
	if(pPlayerName)
	{
		if(pTimeEnd >= pExtension || *pTimeEnd != '_' ||
			std::string_view(pTimeEnd + 1, pExtension - pTimeEnd - 1) != pPlayerName)
			return -1;
	}
	else if(pTimeEnd != pExtension)
		return -1;

	return TimeFromSecondsStr(pTime);
}

int CRaceHelper::TimeFromStr(const char *pStr)
{
	static constexpr const char *MINUTES_STR = " minute(s) ";
	static constexpr const char *SECONDS_STR = " second(s)";
	static constexpr const char *MINUTES_STR_ZH = " 分钟 ";
	static constexpr const char *SECONDS_STR_ZH = " 秒";

	const char *pSeconds = str_find(pStr, SECONDS_STR);
	const bool Chinese = pSeconds == nullptr;
	if(Chinese)
		pSeconds = str_find(pStr, SECONDS_STR_ZH);
	if(!pSeconds)
		return -1;

	const char *pMinutes = str_find(pStr, Chinese ? MINUTES_STR_ZH : MINUTES_STR);
	if(pMinutes)
	{
		while(*pStr == ' ') // skip leading spaces
			pStr++;
		const char *pMinutesSeparator = Chinese ? MINUTES_STR_ZH : MINUTES_STR;
		int SecondsTime = TimeFromSecondsStr(pMinutes + str_length(pMinutesSeparator));
		int Minutes;
		if(SecondsTime == -1 || !ReadTimeNumber(pStr, std::numeric_limits<int>::max() / 60000, Minutes))
			return -1;
		if(Minutes > (std::numeric_limits<int>::max() - SecondsTime) / 60000)
			return -1;
		return Minutes * 60000 + SecondsTime;
	}
	else
	{
		return TimeFromSecondsStr(pStr);
	}
}

int CRaceHelper::TimeFromFinishMessage(const char *pStr, char *pNameBuf, int NameBufSize)
{
	static const char *const s_pFinishedStr = " finished in: ";
	static const char *const s_pFinishedStrZh = " 完成了地图，用时：";
	const char *pFinished = str_find(pStr, s_pFinishedStr);
	const char *pSeparator = s_pFinishedStr;
	if(!pFinished)
	{
		pFinished = str_find(pStr, s_pFinishedStrZh);
		pSeparator = s_pFinishedStrZh;
	}
	if(!pFinished)
		return -1;

	int FinishedPos = pFinished - pStr;
	if(FinishedPos == 0 || FinishedPos >= NameBufSize)
		return -1;

	str_copy(pNameBuf, pStr, FinishedPos + 1);

	return TimeFromStr(pFinished + str_length(pSeparator));
}
