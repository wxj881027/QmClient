#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_SCORE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_SCORE_H

#include <base/str.h>
#include <base/time.h>

#include <engine/serverbrowser.h>

template<size_t N>
inline void QmFormatBrowserScore(char (&aBuffer)[N], const CServerInfo::CClient &Client, CServerInfo::EClientScoreKind Kind)
{
	if(!Client.m_Player)
	{
		str_copy(aBuffer, "SPEC");
		return;
	}
	if(Kind == CServerInfo::CLIENT_SCORE_KIND_POINTS)
	{
		str_format(aBuffer, N, "%d", Client.m_Score);
		return;
	}

	// 先拓宽再取绝对值，保留 int 最小值对应的完整秒数。
	int64_t Seconds = Client.m_Score;
	if(Kind == CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT)
	{
		if(Seconds < 0)
			Seconds = -Seconds;
		if(Seconds == 0 || Seconds == 9999)
			Seconds = -1;
	}
	if(Seconds < 0)
		str_copy(aBuffer, "");
	else
		str_time(Seconds * 100, TIME_HOURS, aBuffer, N);
}

#endif
