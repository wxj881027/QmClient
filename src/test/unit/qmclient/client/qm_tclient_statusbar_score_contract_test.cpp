#include <generated/protocol.h>

#include <game/client/components/tclient/statusbar.h>

#include <gtest/gtest.h>
#include <test/test.h>

TEST(TClientStatusBarScore, FormatsPlayerPointsStates)
{
	char aBuf[32];

	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::READY, 12345));
	EXPECT_STREQ(aBuf, "12345");
	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::NOT_REQUESTED, 0));
	EXPECT_STREQ(aBuf, "...");
	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::FETCHING, 0));
	EXPECT_STREQ(aBuf, "...");
	EXPECT_TRUE(tclient_statusbar::FormatPlayerPoints(aBuf, sizeof(aBuf), EPointsStatus::FAILED, 0));
	EXPECT_STREQ(aBuf, "?");
	EXPECT_FALSE(tclient_statusbar::FormatPlayerPoints(aBuf, 0, EPointsStatus::READY, 0));
}

TEST(TClientStatusBar, ValidatesPlayerIds)
{
	EXPECT_FALSE(tclient_statusbar::IsValidPlayerId(SPEC_FREEVIEW));
	EXPECT_FALSE(tclient_statusbar::IsValidPlayerId(-1));
	EXPECT_TRUE(tclient_statusbar::IsValidPlayerId(0));
	EXPECT_TRUE(tclient_statusbar::IsValidPlayerId(MAX_CLIENTS - 1));
	EXPECT_FALSE(tclient_statusbar::IsValidPlayerId(MAX_CLIENTS));
}
