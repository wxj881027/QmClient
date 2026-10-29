#include <engine/storage.h>

#include <android/asset_integrity.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <string>

namespace
{
	const std::string HASH(64, '0');
	class CFailResource : public IAllocationResource
	{
	public:
		int m_Calls = 0, m_Live = 0;
		int m_FailAt = 1;
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(++m_Calls == m_FailAt)
				throw std::bad_alloc();
			auto *pData = AllocateDefault(Bytes, Alignment);
			++m_Live;
			return pData;
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			--m_Live;
			DeallocateDefault(pData, Bytes, Alignment);
		}
	};

	class CAndroidAssetIntegrityFileTest : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH];
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "index.txt", m_aPath, sizeof(m_aPath));
		}
		void Write(const std::string &Text)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			ASSERT_EQ(io_write(File.get(), Text.data(), static_cast<unsigned>(Text.size())), Text.size());
			EXPECT_EQ(io_close(File.release()), 0);
		}
	};
}

TEST_F(CAndroidAssetIntegrityFileTest, GeneratedShapeProducesHeaderAndCompleteEntries)
{
	ASSERT_NO_FATAL_FAILURE(Write(HASH + "\ndata/测试/file a.png " + HASH + "\ndata/other " + HASH + "\n"));
	const auto Lines = android_assets::ReadIntegrityFile(m_aPath);
	ASSERT_EQ(Lines.size(), 3u);
	EXPECT_STREQ(Lines[0].m_aFilename, "");
	EXPECT_STREQ(Lines[1].m_aFilename, "data/测试/file a.png");
	EXPECT_STREQ(Lines[2].m_aFilename, "data/other");
}

TEST_F(CAndroidAssetIntegrityFileTest, UnsafeLaterEntryDiscardsWholeSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(Write(HASH + "\ndata/valid " + HASH + "\n../user/settings.cfg " + HASH + "\n"));
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}

TEST_F(CAndroidAssetIntegrityFileTest, OversizedHashDoesNotBecomeTruncatedValidDigest)
{
	ASSERT_NO_FATAL_FAILURE(Write(HASH + "\ndata/file " + HASH + "extra\n"));
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}

TEST_F(CAndroidAssetIntegrityFileTest, MissingAggregateHeaderDoesNotLoseFirstFileSilently)
{
	ASSERT_NO_FATAL_FAILURE(Write("data/file " + HASH + "\n"));
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}

TEST_F(CAndroidAssetIntegrityFileTest, InvalidUtf8LineIsNotSkippedAsIfSnapshotWereComplete)
{
	ASSERT_NO_FATAL_FAILURE(Write(HASH + "\ndata/valid " + HASH + "\ndata/" + char(0xff) + " " + HASH + "\n"));
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}

TEST_F(CAndroidAssetIntegrityFileTest, Utf16BomAndCrLfRemainReadable)
{
	std::string Utf16("\xff\xfe", 2);
	for(const char Ch : HASH + "\r\ndata/file " + HASH + "\r\n")
	{
		Utf16.push_back(Ch);
		Utf16.push_back('\0');
	}
	ASSERT_NO_FATAL_FAILURE(Write(Utf16));
	const auto Lines = android_assets::ReadIntegrityFile(m_aPath);
	ASSERT_EQ(Lines.size(), 2u);
	EXPECT_STREQ(Lines[1].m_aFilename, "data/file");
}

TEST_F(CAndroidAssetIntegrityFileTest, RawInputLargerThanBudgetIsRejected)
{
	std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
	ASSERT_TRUE(File);
	ASSERT_EQ(io_seek(File.get(), android_assets::MAX_INDEX_BUFFER_BYTES, IOSEEK_START), 0);
	ASSERT_EQ(io_write(File.get(), "x", 1), 1u);
	ASSERT_EQ(io_close(File.release()), 0);
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}

TEST_F(CAndroidAssetIntegrityFileTest, EntryCountBeyondBudgetDoesNotReturnPrefix)
{
	std::string Index = HASH + "\n";
	for(size_t i = 0; i < android_assets::MAX_INDEX_RECORDS; ++i)
		Index += "data/" + std::to_string(i) + " " + HASH + "\n";
	ASSERT_LT(Index.size(), android_assets::MAX_INDEX_BUFFER_BYTES);
	ASSERT_NO_FATAL_FAILURE(Write(Index));
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}

TEST_F(CAndroidAssetIntegrityFileTest, ExactEntryBudgetRemainsAccepted)
{
	std::string Index = HASH + "\n";
	for(size_t i = 1; i < android_assets::MAX_INDEX_RECORDS; ++i)
		Index += "data/" + std::to_string(i) + " " + HASH + "\n";
	ASSERT_LT(Index.size(), android_assets::MAX_INDEX_BUFFER_BYTES);
	ASSERT_NO_FATAL_FAILURE(Write(Index));
	const auto Lines = android_assets::ReadIntegrityFile(m_aPath);
	EXPECT_EQ(Lines.size(), android_assets::MAX_INDEX_RECORDS);
}

TEST_F(CAndroidAssetIntegrityFileTest, FirstEntryAllocationFailureReturnsEmptyResult)
{
	ASSERT_NO_FATAL_FAILURE(Write(HASH + "\ndata/file " + HASH + "\n"));
	CFailResource Resource;
	const auto Lines = android_assets::ReadIntegrityFile(m_aPath, &Resource);
	EXPECT_TRUE(Lines.empty());
	EXPECT_EQ(Resource.m_Calls, 1);
	EXPECT_EQ(Resource.m_Live, 0);
}

TEST_F(CAndroidAssetIntegrityFileTest, AllocationFailureAfterPrefixReleasesPreparedEntries)
{
	std::string Index = HASH + "\n";
	for(int i = 0; i < 64; ++i)
		Index += "data/" + std::to_string(i) + " " + HASH + "\n";
	ASSERT_NO_FATAL_FAILURE(Write(Index));
	CFailResource Resource;
	Resource.m_FailAt = 2;
	const auto Lines = android_assets::ReadIntegrityFile(m_aPath, &Resource);
	EXPECT_TRUE(Lines.empty());
	EXPECT_EQ(Resource.m_Calls, 2);
	EXPECT_EQ(Resource.m_Live, 0);
}

TEST_F(CAndroidAssetIntegrityFileTest, MissingFileReturnsNoSnapshot)
{
	EXPECT_TRUE(android_assets::ReadIntegrityFile(m_aPath).empty());
}
