#include <test/support/serverbrowser_fixture.h>

using ServerBrowserEntryUpdate = ServerBrowserTests::CServerBrowserFixture;

TEST_F(ServerBrowserEntryUpdate, ReplyReplacesRemoteFieldsAndPreservesLocalIdentity)
{
	const auto Old = Server("Old", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	pEntry->m_Info.m_Favorite = TRISTATE::ALL;
	pEntry->m_Info.m_FavoriteAllowPing = TRISTATE::SOME;
	pEntry->m_Info.m_QmClientCount = 7;
	str_copy(pEntry->m_Info.m_aCommunityId, "local");
	str_copy(pEntry->m_Info.m_aCommunityCountry, "Country");
	str_copy(pEntry->m_Info.m_aCommunityType, "Type");
	const int ServerIndex = pEntry->m_Info.m_ServerIndex;
	pEntry->m_FriendStateValid = true;
	const uint64_t Revision = m_Browser.FriendListRevision();
	auto Fresh = Server("Fresh", 9999, 2);
	Fresh.m_ServerIndex = 999;
	Fresh.m_ReceivedPackets = 9;
	str_copy(Fresh.m_aMap, "New map");
	str_copy(Fresh.m_aCommunityId, "remote");
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Fresh);
	const auto &Result = pEntry->m_Info;
	EXPECT_EQ(m_Browser.Get(0), &Result);
	EXPECT_STREQ(Result.m_aName, "Fresh");
	EXPECT_STREQ(Result.m_aMap, "New map");
	EXPECT_EQ(Result.m_ReceivedPackets, 9U);
	EXPECT_EQ(Result.m_vClients.size(), 2U);
	EXPECT_EQ(Result.m_ServerIndex, ServerIndex);
	EXPECT_EQ(Result.m_QmClientCount, 7);
	EXPECT_EQ(Result.m_Favorite, TRISTATE::ALL);
	EXPECT_EQ(Result.m_FavoriteAllowPing, TRISTATE::SOME);
	EXPECT_EQ(Result.m_NumAddresses, 1);
	EXPECT_EQ(Result.m_aAddresses[0], Old.m_aAddresses[0]);
	EXPECT_STREQ(Result.m_aAddress, "127.0.0.1:8301");
	EXPECT_STREQ(Result.m_aCommunityId, "local");
	EXPECT_STREQ(Result.m_aCommunityCountry, "Country");
	EXPECT_STREQ(Result.m_aCommunityType, "Type");
	EXPECT_EQ(Result.m_HasRank, CServerInfo::RANK_UNAVAILABLE);
	EXPECT_FALSE(pEntry->m_FriendStateValid);
	EXPECT_TRUE(pEntry->m_GotInfo);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision + 2);
	EXPECT_EQ(Fresh.m_aAddresses[0].port, 9999);
	EXPECT_STREQ(Fresh.m_aCommunityId, "remote");
}

TEST_F(ServerBrowserEntryUpdate, PointScoresKeepPlayersFirstAndBreakTiesByName)
{
	Publish({Server("Old", 8301)});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto Fresh = Server("Fresh", 8301, 4);
	str_copy(Fresh.m_aGameType, "DM");
	Fresh.m_vClients[0].m_Score = 10;
	str_copy(Fresh.m_vClients[0].m_aName, "Zulu");
	Fresh.m_vClients[1].m_Score = 10;
	str_copy(Fresh.m_vClients[1].m_aName, "Alpha");
	Fresh.m_vClients[2].m_Score = 50;
	str_copy(Fresh.m_vClients[2].m_aName, "Winner");
	Fresh.m_vClients[3].m_Player = false;
	Fresh.m_vClients[3].m_Score = 100;
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Fresh);
	const auto &Result = *m_Browser.Get(0);
	EXPECT_EQ(Result.m_ClientScoreKind, CServerInfo::CLIENT_SCORE_KIND_POINTS);
	ASSERT_EQ(Result.m_vClients.size(), 4U);
	EXPECT_STREQ(Result.m_vClients[0].m_aName, "Winner");
	EXPECT_STREQ(Result.m_vClients[1].m_aName, "Alpha");
	EXPECT_STREQ(Result.m_vClients[2].m_aName, "Zulu");
	EXPECT_FALSE(Result.m_vClients[3].m_Player);
	const auto Color = CServerInfo::GametypeColor("DM");
	EXPECT_FLOAT_EQ(Result.m_GametypeColor.r, Color.r);
	EXPECT_FLOAT_EQ(Result.m_GametypeColor.g, Color.g);
	EXPECT_FLOAT_EQ(Result.m_GametypeColor.b, Color.b);
	EXPECT_FLOAT_EQ(Result.m_GametypeColor.a, Color.a);
}

TEST_F(ServerBrowserEntryUpdate, InferredRaceKindKeepsFinishedScoresBeforeNegativeScores)
{
	Publish({Server("Old", 8301)});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto Fresh = Server("Fresh", 8301, 3);
	Fresh.m_vClients[0].m_Score = -9999;
	Fresh.m_vClients[1].m_Score = 50;
	Fresh.m_vClients[2].m_Score = 100;
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Fresh);
	const auto &Result = *m_Browser.Get(0);
	EXPECT_EQ(Result.m_ClientScoreKind, CServerInfo::CLIENT_SCORE_KIND_TIME_BACKCOMPAT);
	ASSERT_EQ(Result.m_vClients.size(), 3U);
	EXPECT_EQ(Result.m_vClients[0].m_Score, 100);
	EXPECT_EQ(Result.m_vClients[1].m_Score, 50);
	EXPECT_EQ(Result.m_vClients[2].m_Score, -9999);
}

TEST_F(ServerBrowserEntryUpdate, ExplicitTimeKindKeepsAscendingFinishedScores)
{
	Publish({Server("Old", 8301)});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto Fresh = Server("Fresh", 8301, 3);
	Fresh.m_ClientScoreKind = CServerInfo::CLIENT_SCORE_KIND_TIME;
	Fresh.m_vClients[0].m_Score = -1;
	Fresh.m_vClients[1].m_Score = 100;
	Fresh.m_vClients[2].m_Score = 50;
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Fresh);
	const auto &Result = *m_Browser.Get(0);
	EXPECT_EQ(Result.m_ClientScoreKind, CServerInfo::CLIENT_SCORE_KIND_TIME);
	ASSERT_EQ(Result.m_vClients.size(), 3U);
	EXPECT_EQ(Result.m_vClients[0].m_Score, 50);
	EXPECT_EQ(Result.m_vClients[1].m_Score, 100);
	EXPECT_EQ(Result.m_vClients[2].m_Score, -1);
}

TEST_F(ServerBrowserEntryUpdate, SelfUpdateKeepsPlayersAndInvalidatesFriendCache)
{
	const auto Old = Server("Old", 8301, 2);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	auto *pEntry = m_Browser.Find(Old.m_aAddresses[0]);
	ASSERT_NE(pEntry, nullptr);
	pEntry->m_FriendStateValid = true;
	const uint64_t Revision = m_Browser.FriendListRevision();
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, pEntry->m_Info);
	EXPECT_EQ(m_Browser.Get(0), &pEntry->m_Info);
	ASSERT_EQ(pEntry->m_Info.m_vClients.size(), 2U);
	EXPECT_STREQ(pEntry->m_Info.m_vClients[0].m_aName, "Player0");
	EXPECT_STREQ(pEntry->m_Info.m_vClients[1].m_aName, "Player1");
	EXPECT_FALSE(pEntry->m_FriendStateValid);
	EXPECT_TRUE(pEntry->m_GotInfo);
	EXPECT_EQ(m_Browser.FriendListRevision(), Revision + 2);
}

TEST_F(ServerBrowserEntryUpdate, EmptyReplyRemovesPreviousPlayersAndRecomputesRankFromLocalCommunity)
{
	LoadCommunities();
	const auto Old = Server("Old", 8301);
	Publish({Old});
	ASSERT_EQ(m_Browser.NumServers(), 1);
	ASSERT_EQ(m_Browser.Get(0)->m_HasRank, CServerInfo::RANK_RANKED);
	auto Fresh = Server("Empty", 9999, 0);
	str_copy(Fresh.m_aMap, "Unfinished");
	Fresh.m_HasRank = CServerInfo::RANK_RANKED;
	CServerBrowserTestAccess::SetFirstInfo(m_Browser, Fresh);
	const auto &Result = *m_Browser.Get(0);
	EXPECT_TRUE(Result.m_vClients.empty());
	EXPECT_EQ(Result.m_NumClients, 0);
	EXPECT_EQ(Result.m_HasRank, CServerInfo::RANK_UNRANKED);
	EXPECT_STREQ(Result.m_aCommunityId, "a");
	EXPECT_EQ(Result.m_aAddresses[0], Old.m_aAddresses[0]);
}
