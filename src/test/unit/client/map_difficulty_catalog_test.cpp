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
