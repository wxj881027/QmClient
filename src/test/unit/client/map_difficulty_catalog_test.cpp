#include <base/str.h>

#include <engine/serverbrowser.h>

#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>

#include <cstddef>

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
