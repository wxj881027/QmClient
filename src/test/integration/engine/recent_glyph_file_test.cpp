#include <engine/client/qm_recent_glyphs.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>

namespace
{
	class RecentGlyphFile : public ::testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		static constexpr const char *CACHE_PATH = "qmclient/glyph_prewarm.txt";
		void SetUp() override
		{
			m_Info.m_DeleteTestStorageFilesOnSuccess = false;
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
		}
		void Write(const std::string &Text)
		{
			IOHANDLE File = m_pStorage->OpenFile(CACHE_PATH, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			const auto Written = io_write(File, Text.data(), static_cast<unsigned>(Text.size()));
			const int Closed = io_close(File);
			EXPECT_EQ(Written, Text.size());
			EXPECT_EQ(Closed, 0);
		}
		std::string Read()
		{
			void *pData = nullptr;
			unsigned Size = 0;
			if(!m_pStorage->ReadFile(CACHE_PATH, IStorage::TYPE_SAVE, &pData, &Size))
			{
				ADD_FAILURE();
				return {};
			}
			std::unique_ptr<void, decltype(&free)> Data(pData, &free);
			return std::string(static_cast<const char *>(Data.get()), Size);
		}
	};
}

TEST_F(RecentGlyphFile, SavesAndReloadsTheOriginalPlainTextFormat)
{
	QmRecentGlyphs::CCollection Original;
	Original.Record(65, 16);
	Original.Record(20013, 20);
	ASSERT_TRUE(QmRecentGlyphs::SaveFile(m_pStorage.get(), CACHE_PATH, Original));
	EXPECT_EQ(Read(), "65 16\n20013 20\n");
	QmRecentGlyphs::CCollection Reloaded;
	ASSERT_TRUE(QmRecentGlyphs::LoadFile(m_pStorage.get(), CACHE_PATH, Reloaded));
	EXPECT_EQ(Reloaded.Items(), Original.Items());
}

TEST_F(RecentGlyphFile, CorruptNumericLinesDoNotPreventValidRecordsFromLoading)
{
	Write("2147483648 16\n65 16\r\n66 999999999999999999999\n67 20");
	QmRecentGlyphs::CCollection Collection;
	ASSERT_TRUE(QmRecentGlyphs::LoadFile(m_pStorage.get(), CACHE_PATH, Collection));
	EXPECT_EQ(Collection.Items(), (std::vector<std::pair<int, int>>{{65, 16}, {67, 20}}));
}

TEST_F(RecentGlyphFile, SavingEmptyCollectionRetainsExistingCacheBytes)
{
	Write("65 16\n");
	QmRecentGlyphs::CCollection Empty;
	ASSERT_TRUE(QmRecentGlyphs::SaveFile(m_pStorage.get(), CACHE_PATH, Empty));
	EXPECT_EQ(Read(), "65 16\n");
}

TEST_F(RecentGlyphFile, OverBudgetFileRetainsTheExistingLiveCollection)
{
	Write(std::string(QmRecentGlyphs::MAX_FILE_BYTES + 1, ' '));
	QmRecentGlyphs::CCollection Collection;
	Collection.Record(65, 16);
	EXPECT_FALSE(QmRecentGlyphs::LoadFile(m_pStorage.get(), CACHE_PATH, Collection));
	EXPECT_EQ(Collection.Items(), (std::vector<std::pair<int, int>>{{65, 16}}));
}

TEST_F(RecentGlyphFile, MissingOptionalCacheLeavesTheLiveCollectionUnchanged)
{
	ASSERT_FALSE(m_pStorage->FileExists(CACHE_PATH, IStorage::TYPE_SAVE));
	QmRecentGlyphs::CCollection Collection;
	Collection.Record(65, 16);
	EXPECT_FALSE(QmRecentGlyphs::LoadFile(m_pStorage.get(), CACHE_PATH, Collection));
	EXPECT_EQ(Collection.Items(), (std::vector<std::pair<int, int>>{{65, 16}}));
}
