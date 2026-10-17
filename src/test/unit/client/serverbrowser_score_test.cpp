#include <game/client/components/qmclient/browser_score.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace
{
	std::string FormatScore(int Score, CServerInfo::EClientScoreKind Kind, bool Player = true)
	{
		CServerInfo::CClient Client{};
		Client.m_Score = Score;
		Client.m_Player = Player;
		char aBuffer[16] = "previous";
		QmFormatBrowserScore(aBuffer, Client, Kind);
		return aBuffer;
	}
}

TEST(ServerBrowserScore, LegacyMinimumIntegerIsFormattedWithoutSignedOverflow)
{
	EXPECT_EQ(FormatScore(std::numeric_limits<int>::min(), CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT), "596523:14:08");
	EXPECT_EQ(FormatScore(std::numeric_limits<int>::max(), CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT), "596523:14:07");
}

TEST(ServerBrowserScore, LegacyTimesKeepTheirAbsoluteSeconds)
{
	for(const int Score : {65, -65})
	{
		SCOPED_TRACE(Score);
		EXPECT_EQ(FormatScore(Score, CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT), "01:05");
	}
	EXPECT_EQ(FormatScore(-3601, CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT), "01:00:01");
}

TEST(ServerBrowserScore, LegacyUnsetValuesClearThePreviousLabel)
{
	for(const int Score : {0, 9999, -9999})
	{
		SCOPED_TRACE(Score);
		EXPECT_TRUE(FormatScore(Score, CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT).empty());
	}
}

TEST(ServerBrowserScore, PointsKeepSignedIntegerLimits)
{
	EXPECT_EQ(FormatScore(std::numeric_limits<int>::min(), CServerInfo::CLIENT_SCORE_KIND_POINTS), "-2147483648");
	EXPECT_EQ(FormatScore(std::numeric_limits<int>::max(), CServerInfo::CLIENT_SCORE_KIND_POINTS), "2147483647");
	EXPECT_EQ(FormatScore(0, CServerInfo::CLIENT_SCORE_KIND_POINTS), "0");
}

TEST(ServerBrowserScore, ModernNegativeTimesClearThePreviousLabel)
{
	for(const int Score : {std::numeric_limits<int>::min(), -9999, -1})
	{
		SCOPED_TRACE(Score);
		EXPECT_TRUE(FormatScore(Score, CServerInfo::CLIENT_SCORE_KIND_TIME).empty());
	}
}

TEST(ServerBrowserScore, ModernTimesKeepZeroAndLegacySentinelAsRealSeconds)
{
	EXPECT_EQ(FormatScore(0, CServerInfo::CLIENT_SCORE_KIND_TIME), "00:00");
	EXPECT_EQ(FormatScore(9999, CServerInfo::CLIENT_SCORE_KIND_TIME), "02:46:39");
	EXPECT_EQ(FormatScore(std::numeric_limits<int>::max(), CServerInfo::CLIENT_SCORE_KIND_TIME), "596523:14:07");
}

TEST(ServerBrowserScore, SpectatorsKeepTheirLabelRegardlessOfScoreKind)
{
	for(const auto Kind : {CServerInfo::CLIENT_SCORE_KIND_UNSPECIFIED, CServerInfo::CLIENT_SCORE_KIND_POINTS, CServerInfo::CLIENT_SCORE_KIND_TIME, CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT})
	{
		SCOPED_TRACE(Kind);
		EXPECT_EQ(FormatScore(std::numeric_limits<int>::min(), Kind, false), "SPEC");
	}
}
