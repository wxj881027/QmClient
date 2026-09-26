#include <game/client/components/qmclient/scoreboard_skin.h>

#include <gtest/gtest.h>

TEST(QmScoreboardSkin, CopiesMainAndDummyConfiguration)
{
	CConfig Config;
	Config.m_ClDummy = 0;
	ASSERT_TRUE(QmCopyScoreboardSkin(Config, false, "tw", 1, 10, 20));
	EXPECT_STREQ(Config.m_ClPlayerSkin, "tw");
	EXPECT_EQ(Config.m_ClPlayerColorBody, 10);
	EXPECT_EQ(Config.m_ClPlayerColorFeet, 20);
	Config.m_ClDummy = 1;
	ASSERT_TRUE(QmCopyScoreboardSkin(Config, false, "dummy", 0, 30, 40));
	EXPECT_STREQ(Config.m_ClDummySkin, "dummy");
	EXPECT_EQ(Config.m_ClDummyColorBody, 30);
}

TEST(QmScoreboardSkin, RejectsSixupAndEmptyNames)
{
	CConfig Config;
	EXPECT_FALSE(QmCopyScoreboardSkin(Config, true, "tw", 1, 1, 1));
	EXPECT_FALSE(QmCopyScoreboardSkin(Config, false, "", 1, 1, 1));
}
