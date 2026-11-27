// 本地时间 HUD 格式化：媒体岛、灵动岛与常驻时钟必须共用同一个“显示秒”开关。
// 若任一渲染点退回硬编码 "%H:%M"，无秒断言会失败。
#include <game/client/components/qmclient/local_time_format.h>

#include <gtest/gtest.h>

#include <cstring>

namespace
{
	bool MatchesClockShape(const char *pText, bool WithSeconds)
	{
		const size_t ExpectedLength = WithSeconds ? 8 : 5;
		if(std::strlen(pText) != ExpectedLength)
			return false;
		for(size_t i = 0; i < ExpectedLength; i++)
		{
			const bool Separator = i == 2 || (WithSeconds && i == 5);
			if(Separator)
			{
				if(pText[i] != ':')
					return false;
			}
			else if(pText[i] < '0' || pText[i] > '9')
			{
				return false;
			}
		}
		return true;
	}
}

TEST(QmLocalTimeFormat, IncludesSecondsWhenEnabled)
{
	char aTime[16];
	QmFormatLocalTime(aTime, sizeof(aTime), true);
	EXPECT_TRUE(MatchesClockShape(aTime, true)) << aTime;
}

TEST(QmLocalTimeFormat, OmitsSecondsWhenDisabled)
{
	char aTime[16];
	QmFormatLocalTime(aTime, sizeof(aTime), false);
	EXPECT_TRUE(MatchesClockShape(aTime, false)) << aTime;
}
