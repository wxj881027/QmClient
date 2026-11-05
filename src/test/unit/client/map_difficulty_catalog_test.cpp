#include <base/str.h>

#include <engine/serverbrowser.h>

#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

TEST(MapDifficultyCatalog, LoadsEntriesAndUsesCategoryHint)
{
	constexpr char aSql[] = R"sql(
		CREATE TABLE map_difficulty (map_name TEXT NOT NULL, category TEXT NOT NULL, stars INTEGER NOT NULL);
		INSERT INTO map_difficulty VALUES ('Alpha', 'Novice', 1);
		INSERT INTO map_difficulty VALUES ('Alpha', 'Brutal', 4);
		INSERT INTO map_difficulty VALUES ('Beta', 'Race', 0);
	)sql";

	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(aSql, sizeof(aSql) - 1));
	EXPECT_EQ(Catalog.Size(), 3);

	const CQmMapDifficultyCatalog::SEntry *pBrutal = Catalog.Find("alpha", "BRUTAL");
	ASSERT_NE(pBrutal, nullptr);
	EXPECT_EQ(pBrutal->m_MapName, "Alpha");
	EXPECT_EQ(pBrutal->m_Category, "Brutal");
	EXPECT_EQ(pBrutal->m_Stars, 4);

	const CQmMapDifficultyCatalog::SEntry *pFallback = Catalog.Find("ALPHA");
	ASSERT_NE(pFallback, nullptr);
	EXPECT_EQ(pFallback->m_Category, "Brutal");
	EXPECT_EQ(pFallback->m_Stars, 4);

	const CQmMapDifficultyCatalog::SEntry *pBeta = Catalog.Find("beta", "missing");
	ASSERT_NE(pBeta, nullptr);
	EXPECT_EQ(pBeta->m_Category, "Race");
	EXPECT_EQ(pBeta->m_Stars, 0);
	EXPECT_EQ(Catalog.Find("unknown"), nullptr);
}

TEST(MapDifficultyCatalog, RejectsMissingOrInvalidSchema)
{
	constexpr char aSql[] = "CREATE TABLE unrelated (value TEXT);";
	CQmMapDifficultyCatalog Catalog;

	EXPECT_FALSE(Catalog.LoadSqlText(aSql, sizeof(aSql) - 1));
	EXPECT_TRUE(Catalog.Empty());
}

namespace
{
	constexpr char s_aServerSql[] = R"sql(
		CREATE TABLE map_difficulty (map_name TEXT NOT NULL, category TEXT NOT NULL, stars INTEGER NOT NULL);
		INSERT INTO map_difficulty VALUES ('SharedMap', 'Novice', 1);
		INSERT INTO map_difficulty VALUES ('SharedMap', 'Brutal', 4);
	)sql";
}

TEST(MapDifficultyCatalog, DdraceServersKeepTheirMapStarsAndCategoryHint)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(s_aServerSql, sizeof(s_aServerSql) - 1));
	CServerInfo Server{};
	str_copy(Server.m_aMap, "sharedmap");
	str_copy(Server.m_aCommunityId, IServerBrowser::COMMUNITY_NONE);
	for(const char *pGameType : {"DDNet", "DDRaceNetwork", "ddrace"})
	{
		SCOPED_TRACE(pGameType);
		str_copy(Server.m_aGameType, pGameType);
		const auto *pDifficulty = Catalog.FindForServer(Server, "NOVICE");
		ASSERT_NE(pDifficulty, nullptr);
		EXPECT_EQ(pDifficulty->m_Stars, 1);
	}
}

TEST(MapDifficultyCatalog, GoresServersNeverUseSameNamedDdraceMapStars)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(s_aServerSql, sizeof(s_aServerSql) - 1));
	CServerInfo Server{};
	str_copy(Server.m_aMap, "SharedMap");
	str_copy(Server.m_aName, "Novice");
	const struct
	{
		const char *m_pGameType;
		const char *m_pCommunityId;
		const char *m_pCommunityType;
	} aGoresServers[] = {
		{"Gores", "none", "None"},
		{"DDNet Gores", "none", "None"},
		{"DDNet", "axiom", "gores"},
		{"DDNet", "KoG", "Novice"},
		{"Gores", "ddnet", "Novice"},
	};
	for(const auto &Case : aGoresServers)
	{
		SCOPED_TRACE(Case.m_pGameType);
		SCOPED_TRACE(Case.m_pCommunityId);
		SCOPED_TRACE(Case.m_pCommunityType);
		str_copy(Server.m_aGameType, Case.m_pGameType);
		str_copy(Server.m_aCommunityId, Case.m_pCommunityId);
		str_copy(Server.m_aCommunityType, Case.m_pCommunityType);
		EXPECT_EQ(Catalog.FindForServer(Server, "Novice"), nullptr);
	}
}

TEST(MapDifficultyCatalog, ServerModeChangesReevaluateSameNamedMapStars)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(s_aServerSql, sizeof(s_aServerSql) - 1));
	CServerInfo Server{};
	str_copy(Server.m_aMap, "SharedMap");
	str_copy(Server.m_aGameType, "DDNet");
	const auto *pDifficulty = Catalog.FindForServer(Server);
	ASSERT_NE(pDifficulty, nullptr);
	EXPECT_EQ(pDifficulty->m_Stars, 4);

	str_copy(Server.m_aGameType, "Gores");
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);

	str_copy(Server.m_aGameType, "DDRaceNetwork");
	EXPECT_EQ(Catalog.FindForServer(Server), pDifficulty);
}

TEST(MapDifficultyCatalog, UnknownAndUnrelatedServerModesDoNotMatchByMapName)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(s_aServerSql, sizeof(s_aServerSql) - 1));
	CServerInfo Server{};
	str_copy(Server.m_aMap, "SharedMap");
	str_copy(Server.m_aName, "DDNet Novice");
	for(const char *pGameType : {"", "CTF", "Race", "AXRace"})
	{
		SCOPED_TRACE(pGameType);
		str_copy(Server.m_aGameType, pGameType);
		EXPECT_EQ(Catalog.FindForServer(Server, "Novice"), nullptr);
	}

	str_copy(Server.m_aGameType, "DDNet");
	str_copy(Server.m_aMap, "UnknownMap");
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
}

namespace
{
	CServerInfo AxiomServer(const char *pMode = "Gores", const char *pMap = "SharedMap")
	{
		CServerInfo Server{};
		str_copy(Server.m_aCommunityId, "axiom");
		str_copy(Server.m_aGameType, pMode);
		str_copy(Server.m_aMap, pMap);
		return Server;
	}

	std::string AxiomMotd(const char *pMap, const char *pStars, const char *pCategory = "简单")
	{
		return std::string("   ⌬ Axiom ✦ Gores - ") + pCategory + "\n\n┌──── 当前地图信息\n│ 地图：" + pMap +
		       "\n│ 作者：Mapper\n│ 类型：" + pCategory + "\n│ 星级：" + pStars + "\n│ 分数：2\n└─────────────────────\n";
	}
}

TEST(MapDifficultyCatalog, AxiomAnnouncementProvidesThreeStarsAndDoesNotReplaceDdraceData)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(s_aServerSql, sizeof(s_aServerSql) - 1));
	const CServerInfo Server = AxiomServer();
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", "★★✰").c_str()));
	const auto *pEntry = Catalog.FindForServer(Server, "Novice");
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Stars, 2);
	EXPECT_EQ(pEntry->m_MaxStars, 3);
	EXPECT_EQ(pEntry->m_Category, "简单");
	EXPECT_STREQ(pEntry->StarLabel(), "★★☆");
	ASSERT_NE(Catalog.Find("SharedMap", "Novice"), nullptr);
	EXPECT_EQ(Catalog.Find("SharedMap", "Novice")->m_Stars, 1);
}

TEST(MapDifficultyCatalog, AxiomModesKeepIndependentRatingsForSameMap)
{
	CQmMapDifficultyCatalog Catalog;
	const CServerInfo Gores = AxiomServer();
	const CServerInfo Race = AxiomServer("AXRace");
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Gores, AxiomMotd("SharedMap", "★★★").c_str()));
	EXPECT_EQ(Catalog.FindForServer(Race), nullptr);
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Race, AxiomMotd("SharedMap", "★★★★✰", "单人").c_str()));
	ASSERT_NE(Catalog.FindForServer(Gores), nullptr);
	ASSERT_NE(Catalog.FindForServer(Race), nullptr);
	EXPECT_STREQ(Catalog.FindForServer(Gores)->StarLabel(), "★★★");
	EXPECT_STREQ(Catalog.FindForServer(Race)->StarLabel(), "★★★★☆");
}

TEST(MapDifficultyCatalog, RepeatedAxiomAnnouncementDoesNotChangeCacheAndNewRatingReplacesOld)
{
	CQmMapDifficultyCatalog Catalog;
	const CServerInfo Server = AxiomServer();
	const std::string Motd = AxiomMotd("SharedMap", "★✰✰");
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, Motd.c_str()));
	const std::string Before = Catalog.SerializeAxiomCache();
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, Motd.c_str()));
	EXPECT_EQ(Catalog.SerializeAxiomCache(), Before);
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", "★★★", "普通").c_str()));
	const auto *pEntry = Catalog.FindForServer(Server);
	ASSERT_NE(pEntry, nullptr);
	EXPECT_EQ(pEntry->m_Stars, 3);
	EXPECT_EQ(pEntry->m_Category, "普通");
}

TEST(MapDifficultyCatalog, AxiomAnnouncementRequiresCurrentMapAndVerifiedCommunity)
{
	CQmMapDifficultyCatalog Catalog;
	CServerInfo Server = AxiomServer();
	const std::string Motd = AxiomMotd("SharedMap", "★★✰");
	str_copy(Server.m_aCommunityId, "none");
	str_copy(Server.m_aName, "Axiom 普通");
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, Motd.c_str()));
	str_copy(Server.m_aCommunityId, "axiom");
	str_copy(Server.m_aMap, "NextMap");
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, Motd.c_str()));
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
	str_copy(Server.m_aMap, "SharedMap");
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, Motd.c_str()));
	for(const char *pCommunity : {"kog", "ddnet", "none"})
	{
		SCOPED_TRACE(pCommunity);
		str_copy(Server.m_aCommunityId, pCommunity);
		EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
	}
}

TEST(MapDifficultyCatalog, InvalidAxiomAnnouncementPreservesLastKnownRating)
{
	CQmMapDifficultyCatalog Catalog;
	const CServerInfo Server = AxiomServer();
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", "★★✰").c_str()));
	const std::string Before = Catalog.SerializeAxiomCache();
	for(const char *pStars : {"", "★", "★★★★", "★★✰✰✰", "☆★☆", "2", "★bad"})
	{
		SCOPED_TRACE(pStars);
		EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", pStars).c_str()));
	}
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, nullptr));
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, "Axiom 地图读取失败"));
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, (AxiomMotd("SharedMap", "★★✰") + "│ 星级：★★★\n").c_str()));
	EXPECT_EQ(Catalog.SerializeAxiomCache(), Before);
}

TEST(MapDifficultyCatalog, AxiomCacheRoundTripRestoresBothModesAndUnknownMapsRemainUnknown)
{
	CQmMapDifficultyCatalog Original;
	ASSERT_TRUE(Original.RecordAxiomMotd(AxiomServer(), AxiomMotd("SharedMap", "★☆☆").c_str()));
	ASSERT_TRUE(Original.RecordAxiomMotd(AxiomServer("AXRace"), AxiomMotd("SharedMap", "★★★★★").c_str()));
	const std::string Json = Original.SerializeAxiomCache();
	CQmMapDifficultyCatalog Restored;
	ASSERT_TRUE(Restored.LoadAxiomCacheJson(Json.data(), Json.size()));
	ASSERT_NE(Restored.FindForServer(AxiomServer()), nullptr);
	EXPECT_STREQ(Restored.FindForServer(AxiomServer())->StarLabel(), "★☆☆");
	ASSERT_NE(Restored.FindForServer(AxiomServer("AXRace")), nullptr);
	EXPECT_STREQ(Restored.FindForServer(AxiomServer("AXRace"))->StarLabel(), "★★★★★");
	EXPECT_EQ(Restored.FindForServer(AxiomServer("Gores", "NeverVisited")), nullptr);
	EXPECT_EQ(Restored.Find("SharedMap"), nullptr);
}

TEST(MapDifficultyCatalog, InvalidAxiomCacheDoesNotReplaceValidRecords)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.RecordAxiomMotd(AxiomServer(), AxiomMotd("SharedMap", "★★★").c_str()));
	const std::string Before = Catalog.SerializeAxiomCache();
	const char *apInvalid[] = {
		"not json",
		R"({"version":2,"entries":[]})",
		R"({"version":1,"entries":[{"map":"SharedMap","category":"Easy","mode":"Gores","stars":4}]})",
		R"({"version":1,"entries":[{"map":"SharedMap","category":"Easy","mode":"AXRace","stars":-1}]})",
		R"({"version":1,"entries":[{"map":"SharedMap","category":"Easy","mode":"KoG","stars":2}]})",
		R"({"version":1,"entries":[{"map":"SharedMap\u0000other","category":"Easy","mode":"Gores","stars":2}]})",
		R"({"version":1,"entries":[{"map":"SharedMap","category":"Easy","mode":"Gores","stars":"2"}]})",
		R"({"version":1,"entries":[{"map":"Same","category":"Easy","mode":"Gores","stars":1},{"map":"Same","category":"Easy","mode":"Gores","stars":3}]})",
	};
	for(const char *pJson : apInvalid)
	{
		SCOPED_TRACE(pJson);
		EXPECT_FALSE(Catalog.LoadAxiomCacheJson(pJson, str_length(pJson)));
		EXPECT_EQ(Catalog.SerializeAxiomCache(), Before);
	}
}

TEST(MapDifficultyCatalog, CompatibleAxiomGameTypeRequiresCommunityModeAndUsesItsStarScale)
{
	CQmMapDifficultyCatalog Catalog;
	CServerInfo Server = AxiomServer("DDNet");
	EXPECT_FALSE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", "★★★").c_str()));
	str_copy(Server.m_aCommunityType, "Gores");
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", "★★★").c_str()));
	ASSERT_NE(Catalog.FindForServer(Server), nullptr);
	EXPECT_STREQ(Catalog.FindForServer(Server)->StarLabel(), "★★★");
	str_copy(Server.m_aCommunityType, "Other");
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, AxiomMotd("SharedMap", "★★☆☆☆").c_str()));
	ASSERT_NE(Catalog.FindForServer(Server), nullptr);
	EXPECT_STREQ(Catalog.FindForServer(Server)->StarLabel(), "★★☆☆☆");
	str_copy(Server.m_aGameType, "CTF");
	EXPECT_EQ(Catalog.FindForServer(Server), nullptr);
}

TEST(MapDifficultyCatalog, FailedAxiomSaveKeepsRecordsDirtyForRetry)
{
	CQmMapDifficultyCatalog Catalog;
	EXPECT_FALSE(Catalog.AxiomCacheDirty());
	const std::string Motd = AxiomMotd("SharedMap", "★★★");
	ASSERT_TRUE(Catalog.RecordAxiomMotd(AxiomServer(), Motd.c_str()));
	EXPECT_TRUE(Catalog.AxiomCacheDirty());
	EXPECT_FALSE(Catalog.SaveAxiomCache(nullptr));
	EXPECT_FALSE(Catalog.RecordAxiomMotd(AxiomServer(), Motd.c_str()));
	EXPECT_TRUE(Catalog.AxiomCacheDirty());
	ASSERT_NE(Catalog.FindForServer(AxiomServer()), nullptr);
	EXPECT_EQ(Catalog.FindForServer(AxiomServer())->m_Stars, 3);
}

TEST(MapDifficultyCatalog, AxiomCarriageReturnsAndQuotedMapNamesSurviveCacheRoundTrip)
{
	const char *pMap = "Map's \"quote\"";
	const CServerInfo Server = AxiomServer("Gores", pMap);
	const std::string Motd = std::string("Axiom\r\n┌──── 当前地图信息\r\n│ 地图：") + pMap +
				 "\r\n│ 类型：单人\r\n│ 星级：★✰✰\r\n";
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.RecordAxiomMotd(Server, Motd.c_str()));
	const std::string Json = Catalog.SerializeAxiomCache();
	CQmMapDifficultyCatalog Restored;
	ASSERT_TRUE(Restored.LoadAxiomCacheJson(Json.data(), Json.size()));
	EXPECT_FALSE(Restored.AxiomCacheDirty());
	ASSERT_NE(Restored.FindForServer(Server), nullptr);
	EXPECT_EQ(Restored.FindForServer(Server)->m_MapName, pMap);
	EXPECT_STREQ(Restored.FindForServer(Server)->StarLabel(), "★☆☆");
}
