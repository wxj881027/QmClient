#include <engine/storage.h>

#include <game/client/components/qmclient/map_difficulty_catalog.h>

#include <gtest/gtest.h>
#include <test/support/map_difficulty_axiom.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#include <stdexcept>

namespace
{
	using namespace map_difficulty_test;
	constexpr char CACHE_PATH[] = "qmclient/axiom_map_difficulty.json";
	class CMapDifficultyAxiomFile : public testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		CQmMapDifficultyCatalog m_Catalog;
		void SetUp() override
		{
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			ASSERT_NO_FATAL_FAILURE(Write("qmclient/map_difficulty.sql", "CREATE TABLE map_difficulty(map_name,category,stars); INSERT INTO map_difficulty VALUES('Ddrace','Novice',1);"));
			ASSERT_TRUE(m_Catalog.RecordAxiomMotd(Server(), Motd("Old", "★☆☆").c_str()));
		}
		void Write(const char *pPath, const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(!File)
				throw std::runtime_error("could not create isolated Axiom fixture");
			const auto Written = io_write(File, Text.data(), static_cast<unsigned>(Text.size()));
			const int Closed = io_close(File);
			ASSERT_EQ(Written, Text.size());
			ASSERT_EQ(Closed, 0);
		}
		std::string Read(const char *pPath)
		{
			std::string Text(4096, '\0');
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE);
			if(!File)
				throw std::runtime_error("could not read isolated Axiom fixture");
			const unsigned Size = io_read(File, Text.data(), static_cast<unsigned>(Text.size()));
			const int Error = io_error(File);
			const int Closed = io_close(File);
			EXPECT_EQ(Error, 0);
			EXPECT_EQ(Closed, 0);
			Text.resize(Size);
			return Text;
		}
	};
}

TEST_F(CMapDifficultyAxiomFile, SerializationFailureKeepsExistingFilesAndDirtyCacheForRetry)
{
	ASSERT_TRUE(m_Catalog.SaveAxiomCache(m_pStorage.get()));
	const std::string Previous = Read(CACHE_PATH);
	ASSERT_TRUE(m_Catalog.RecordAxiomMotd(Server("New"), Motd("New", "★★★").c_str()));
	char aCandidate[IO_MAX_PATH_LENGTH];
	str_format(aCandidate, sizeof(aCandidate), "%s.%d.tmp", CACHE_PATH, pid());
	ASSERT_NO_FATAL_FAILURE(Write(aCandidate, "previous candidate"));
	bool Saved, Triggered;
	{
		CPredictionAllocationFailure Fault(0);
		Saved = m_Catalog.SaveAxiomCache(m_pStorage.get());
		Triggered = Fault.Triggered();
	}
	ASSERT_TRUE(Triggered);
	EXPECT_FALSE(Saved);
	EXPECT_EQ(Read(aCandidate), "previous candidate");
	EXPECT_EQ(Read(CACHE_PATH), Previous);
	EXPECT_TRUE(m_Catalog.AxiomCacheDirty());
	ASSERT_NE(m_Catalog.FindForServer(Server("New")), nullptr);
	ASSERT_TRUE(m_Catalog.SaveAxiomCache(m_pStorage.get()));
	EXPECT_FALSE(m_Catalog.AxiomCacheDirty());
	CQmMapDifficultyCatalog Restored;
	const std::string Current = Read(CACHE_PATH);
	ASSERT_TRUE(Restored.LoadAxiomCacheJson(Current.data(), Current.size()));
	ASSERT_NE(Restored.FindForServer(Server("New")), nullptr);
	EXPECT_EQ(Restored.FindForServer(Server("New"))->m_Stars, 3);
}

TEST_F(CMapDifficultyAxiomFile, InvalidCacheReloadKeepsPublishedEntriesUntilValidReplacement)
{
	const auto *pOld = m_Catalog.FindForServer(Server());
	ASSERT_NE(pOld, nullptr);
	ASSERT_NO_FATAL_FAILURE(Write(CACHE_PATH, "invalid cache"));
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	EXPECT_EQ(m_Catalog.FindForServer(Server()), pOld);
	EXPECT_TRUE(m_Catalog.AxiomCacheDirty());
	EXPECT_EQ(pOld->m_Stars, 1);
	ASSERT_NO_FATAL_FAILURE(Write(CACHE_PATH, OLD_CACHE));
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	ASSERT_NE(m_Catalog.FindForServer(Server()), nullptr);
	EXPECT_FALSE(m_Catalog.AxiomCacheDirty());
	EXPECT_EQ(m_Catalog.FindForServer(Server())->m_Category, "Old");
}

TEST_F(CMapDifficultyAxiomFile, MissingCacheDoesNotDiscardSessionRatingsOrDirtyState)
{
	const auto *pOld = m_Catalog.FindForServer(Server());
	ASSERT_NE(pOld, nullptr);
	ASSERT_TRUE(m_Catalog.Load(m_pStorage.get(), nullptr));
	EXPECT_EQ(m_Catalog.FindForServer(Server()), pOld);
	EXPECT_TRUE(m_Catalog.AxiomCacheDirty());
	ASSERT_TRUE(m_Catalog.SaveAxiomCache(m_pStorage.get()));
	EXPECT_FALSE(m_Catalog.AxiomCacheDirty());
	EXPECT_FALSE(Read(CACHE_PATH).empty());
}
