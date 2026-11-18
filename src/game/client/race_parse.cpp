#include "race.h"

#include <base/system.h>

#include <iterator>

int CRaceHelper::TimeFromSecondsStr(const char *pStr)
{
	while(*pStr == ' ') // skip leading spaces
		pStr++;
	if(!str_isnum(*pStr))
		return -1;
	int Time = str_toint(pStr) * 1000;
	while(str_isnum(*pStr))
		pStr++;
	if(*pStr == '.' || *pStr == ',')
	{
		pStr++;
		static const int s_aMult[3] = {100, 10, 1};
		for(size_t i = 0; i < std::size(s_aMult) && str_isnum(pStr[i]); i++)
			Time += (pStr[i] - '0') * s_aMult[i];
	}
	return Time;
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
		if(SecondsTime == -1 || !str_isnum(*pStr))
			return -1;
		return str_toint(pStr) * 60 * 1000 + SecondsTime;
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
