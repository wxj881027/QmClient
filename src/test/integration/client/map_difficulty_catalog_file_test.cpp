#include <engine/storage.h>

#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstring>

namespace
{
	constexpr const char *PATH = "qmclient/map_difficulty.sql";
	constexpr const char *SQL = "CREATE TABLE map_difficulty(map_name TEXT,category TEXT,stars INTEGER);"
				    "INSERT INTO map_difficulty VALUES('New','Brutal',4);";
	constexpr size_t MAX_BYTES = 2 * 1024 * 1024;
	class MapDifficultyCatalogFile : public ::testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		CQmMapDifficultyCatalog m_Catalog;
		const CQmMapDifficultyCatalog::SEntry *m_pOld = nullptr;
		void SetUp() override
		{
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			constexpr char aOld[] = "CREATE TABLE map_difficulty(map_name TEXT,category TEXT,stars INTEGER);"
						"INSERT INTO map_difficulty VALUES('Old','Novice',1);";
			ASSERT_TRUE(m_Catalog.LoadSqlText(aOld, sizeof(aOld) - 1));
			m_pOld = m_Catalog.Find("Old");
			ASSERT_NE(m_pOld, nullptr);
		}
		void Write(const std::string &Text)
		{
			const IOHANDLE File = m_pStorage->OpenFile(PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			const unsigned Written = io_write(File, Text.data(), static_cast<unsigned>(Text.size()));
			const int Closed = io_close(File);
			EXPECT_EQ(Written, Text.size());
			EXPECT_EQ(Closed, 0);
		}
		void ExpectOld()
		{
			ASSERT_EQ(m_Catalog.Find("Old"), m_pOld);
			EXPECT_EQ(m_pOld->m_Stars, 1);
			EXPECT_EQ(m_Catalog.Size(), 1);
		}
	};
}

TEST_F(MapDifficultyCatalogFile, LoadsSqlThroughTheExistingStorageSearch)
{
	ASSERT_NO_FATAL_FAILURE(Write(SQL));
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	EXPECT_EQ(m_Catalog.Find("Old"), nullptr);
	ASSERT_NE(m_Catalog.Find("New"), nullptr);
	EXPECT_EQ(m_Catalog.Find("New")->m_Stars, 4);
}

TEST_F(MapDifficultyCatalogFile, FileWithOnlyRejectedRowsKeepsTheOldDirectory)
{
	ASSERT_NO_FATAL_FAILURE(Write("CREATE TABLE map_difficulty(map_name,category,stars);"
				      "INSERT INTO map_difficulty VALUES('Bad','Novice',4294967297);"));
	EXPECT_FALSE(m_Catalog.Load(m_pStorage.get(), nullptr));
	ExpectOld();
}

TEST_F(MapDifficultyCatalogFile, MissingStorageKeepsTheOldDirectory)
{
	EXPECT_FALSE(m_Catalog.Load(nullptr, nullptr));
	ExpectOld();
}

TEST_F(MapDifficultyCatalogFile, OverBudgetFileKeepsOldRowsAndAllowsAValidReload)
{
	std::string Text = SQL;
	Text.resize(MAX_BYTES + 1, ' ');
	ASSERT_NO_FATAL_FAILURE(Write(Text));
	EXPECT_FALSE(m_Catalog.Load(m_pStorage.get(), nullptr));
	ExpectOld();
	ASSERT_NO_FATAL_FAILURE(Write(SQL));
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	EXPECT_NE(m_Catalog.Find("New"), nullptr);
}

TEST_F(MapDifficultyCatalogFile, ExactExistingByteLimitStillLoadsSuccessfully)
{
	std::string Text = SQL;
	Text.resize(MAX_BYTES, ' ');
	ASSERT_NO_FATAL_FAILURE(Write(Text));
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	EXPECT_NE(m_Catalog.Find("New"), nullptr);
}

TEST_F(MapDifficultyCatalogFile, InvalidSqlKeepsOldRowsAndCanBeReplacedByAValidFile)
{
	ASSERT_NO_FATAL_FAILURE(Write("invalid SQL"));
	EXPECT_FALSE(m_Catalog.Load(m_pStorage.get(), nullptr));
	ExpectOld();
	ASSERT_NO_FATAL_FAILURE(Write(SQL));
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	EXPECT_NE(m_Catalog.Find("New"), nullptr);
}

TEST_F(MapDifficultyCatalogFile, EmbeddedNulFileDoesNotPublishItsValidPrefix)
{
	ASSERT_NO_FATAL_FAILURE(Write(std::string(SQL) + '\0' + "invalid SQL"));
	EXPECT_FALSE(m_Catalog.Load(m_pStorage.get(), nullptr));
	ExpectOld();
}

TEST_F(MapDifficultyCatalogFile, EmptyFileKeepsTheOldDirectory)
{
	ASSERT_NO_FATAL_FAILURE(Write(""));
	EXPECT_FALSE(m_Catalog.Load(m_pStorage.get(), nullptr));
	ExpectOld();
}
