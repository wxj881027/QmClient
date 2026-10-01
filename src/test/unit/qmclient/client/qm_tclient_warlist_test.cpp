#include <game/client/components/tclient/warlist.h>

#include <gtest/gtest.h>

TEST(QmWarListEnemyChat, OnlyBuiltInEnemyGroupMatches)
{
	CWarDataCache WarData;
	ASSERT_GE(WarData.m_WarGroupMatches.size(), 3u);

	WarData.m_WarGroupMatches[2] = true;
	EXPECT_FALSE(CWarList::MatchesEnemyGroup(WarData));

	WarData.m_WarGroupMatches[2] = false;
	WarData.m_WarGroupMatches[1] = true;
	EXPECT_TRUE(CWarList::MatchesEnemyGroup(WarData));
}

TEST(QmWarListEnemyChat, ShortGroupDataDoesNotMatchEnemy)
{
	CWarDataCache WarData;
	WarData.m_WarGroupMatches.resize(1);
	EXPECT_FALSE(CWarList::MatchesEnemyGroup(WarData));
}
