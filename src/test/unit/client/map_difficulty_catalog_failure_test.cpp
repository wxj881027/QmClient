#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <cstring>

namespace
{
	constexpr const char *OLD = "CREATE TABLE map_difficulty(map_name TEXT,category TEXT,stars INTEGER);"
				    "INSERT INTO map_difficulty VALUES('Old','Novice',1);";
	constexpr const char *NEW = "CREATE TABLE map_difficulty(map_name TEXT,category TEXT,stars INTEGER);"
				    "INSERT INTO map_difficulty VALUES('New','Brutal',4);";
}

TEST(MapDifficultyCatalogFailure, FailedReloadKeepsExistingRowsAndBorrowedEntries)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(OLD, std::strlen(OLD)));
	const auto *pOld = Catalog.Find("Old");
	ASSERT_NE(pOld, nullptr);
	for(const char *pSql : {"broken", "CREATE TABLE unrelated(value);", "CREATE TABLE map_difficulty(map_name,category,stars);"})
	{
		SCOPED_TRACE(pSql);
		EXPECT_FALSE(Catalog.LoadSqlText(pSql, std::strlen(pSql)));
		EXPECT_EQ(Catalog.Find("old"), pOld);
		EXPECT_EQ(Catalog.Size(), 1);
		EXPECT_EQ(pOld->m_Stars, 1);
	}
	EXPECT_FALSE(Catalog.LoadSqlText(nullptr, 10));
	EXPECT_FALSE(Catalog.LoadSqlText("", 0));
	EXPECT_FALSE(Catalog.LoadSqlText("", 2 * 1024 * 1024 + 1));
	EXPECT_EQ(Catalog.Find("Old"), pOld);
}

TEST(MapDifficultyCatalogFailure, SuccessfulReloadReplacesTheDirectory)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(OLD, std::strlen(OLD)));
	ASSERT_TRUE(Catalog.LoadSqlText(NEW, std::strlen(NEW)));
	EXPECT_EQ(Catalog.Find("Old"), nullptr);
	ASSERT_NE(Catalog.Find("New"), nullptr);
	EXPECT_EQ(Catalog.Find("New")->m_Stars, 4);
	EXPECT_EQ(Catalog.Size(), 1);
}

TEST(MapDifficultyCatalogFailure, QueryEvaluationFailureDoesNotReplaceTheDirectory)
{
	constexpr char aSql[] = "CREATE VIEW map_difficulty AS SELECT 'Bad' AS map_name,'Novice' AS category,abs(-9223372036854775808) AS stars;";
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(OLD, std::strlen(OLD)));
	const auto *pOld = Catalog.Find("Old");
	EXPECT_FALSE(Catalog.LoadSqlText(aSql, sizeof(aSql) - 1));
	EXPECT_EQ(Catalog.Find("Old"), pOld);
	EXPECT_EQ(Catalog.Size(), 1);
}

TEST(MapDifficultyCatalogFailure, MalformedRowsDoNotHideValidRows)
{
	constexpr char aSql[] = R"sql(
CREATE TABLE map_difficulty(map_name,category,stars);
INSERT INTO map_difficulty VALUES ('Real','Novice',4),('Zero','Race',0),
(NULL,'Novice',1),('','Novice',1),('NoCategory',NULL,1),('EmptyCategory','',1),
('Name' || char(0) || 'Suffix','Novice',1),('BadCategory','No' || char(0) || 'vice',1),
(CAST(X'ff' AS TEXT),'Novice',1),('UtfCategory',CAST(X'ff' AS TEXT),1),
('NullStars','Novice',NULL),('RealStars','Novice',1.5),('TextStars','Novice','2');
)sql";
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(aSql, sizeof(aSql) - 1));
	EXPECT_EQ(Catalog.Size(), 2);
	ASSERT_NE(Catalog.Find("Real"), nullptr);
	ASSERT_NE(Catalog.Find("Zero"), nullptr);
	EXPECT_EQ(Catalog.Find("Real")->m_Stars, 4);
	EXPECT_EQ(Catalog.Find("Zero")->m_Stars, 0);
}

TEST(MapDifficultyCatalogFailure, WideIntegersCannotWrapToAValidStarRating)
{
	constexpr char aSql[] = "CREATE TABLE map_difficulty(map_name,category,stars);"
				"INSERT INTO map_difficulty VALUES ('Valid','Novice',1),('Wide','Novice',4294967297),"
				"('NegativeWide','Novice',-4294967295),('Huge','Novice',9223372036854775807);";
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(aSql, sizeof(aSql) - 1));
	EXPECT_EQ(Catalog.Size(), 1);
	EXPECT_EQ(Catalog.Find("Wide"), nullptr);
	EXPECT_EQ(Catalog.Find("NegativeWide"), nullptr);
	EXPECT_EQ(Catalog.Find("Huge"), nullptr);
}

TEST(MapDifficultyCatalogFailure, EmbeddedSqlNulCannotPublishOnlyTheValidPrefix)
{
	CQmMapDifficultyCatalog Catalog;
	ASSERT_TRUE(Catalog.LoadSqlText(OLD, std::strlen(OLD)));
	const auto *pOld = Catalog.Find("Old");
	const std::string Sql = std::string(NEW) + '\0' + "invalid trailing SQL";
	EXPECT_FALSE(Catalog.LoadSqlText(Sql.data(), Sql.size()));
	EXPECT_EQ(Catalog.Find("Old"), pOld);
}

TEST(MapDifficultyCatalogFailure, EveryCppAllocationFailureKeepsTheOldDirectoryAndCanRetry)
{
	std::string Sql = "CREATE TABLE map_difficulty(map_name TEXT,category TEXT,stars INTEGER);";
	for(int Row = 0; Row < 8; ++Row)
		Sql += "INSERT INTO map_difficulty VALUES('" + std::string(70, 'A' + Row) + "','" + std::string(70, 'N') + "',2);";
	int Failures = 0;
	bool Completed = false;
	for(size_t Budget = 0; Budget < 96; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmMapDifficultyCatalog Catalog;
		ASSERT_TRUE(Catalog.LoadSqlText(OLD, std::strlen(OLD)));
		const auto *pOld = Catalog.Find("Old");
		bool Loaded, Triggered;
		{
			CPredictionAllocationFailure Fault(Budget);
			Loaded = Catalog.LoadSqlText(Sql.data(), Sql.size());
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Loaded);
			EXPECT_EQ(Catalog.Size(), 8);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_FALSE(Loaded);
		EXPECT_EQ(Catalog.Find("Old"), pOld);
		EXPECT_EQ(Catalog.Size(), 1);
		EXPECT_EQ(pOld->m_Stars, 1);
		ASSERT_TRUE(Catalog.LoadSqlText(Sql.data(), Sql.size()));
		EXPECT_EQ(Catalog.Size(), 8);
		EXPECT_EQ(Catalog.Find("Old"), nullptr);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(Completed);
}
