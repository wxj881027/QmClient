#include <base/fs.h>
#include <base/system.h>

#include <engine/storage.h>

#include <android/asset_unpack.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	class CAndroidAssetUnpackFileTest : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aInput[IO_MAX_PATH_LENGTH], m_aCandidate[IO_MAX_PATH_LENGTH], m_aOfficial[IO_MAX_PATH_LENGTH];
		bool m_OutputCloseFailure = false;
		bool m_RenameFailure = false;

		void WriteFile(const char *pPath, const std::string &Bytes)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(pPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			ASSERT_EQ(io_write(File.get(), Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
			EXPECT_EQ(io_close(File.release()), 0);
		}
		std::string ReadFile(const char *pPath)
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(pPath, IOFLAG_READ), &io_close);
			EXPECT_TRUE(File);
			if(!File)
				return {};
			char aBytes[128];
			const unsigned Read = io_read(File.get(), aBytes, sizeof(aBytes));
			EXPECT_EQ(io_error(File.get()), 0);
			EXPECT_EQ(io_close(File.release()), 0);
			return std::string(aBytes, Read);
		}
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "input.bin", m_aInput, sizeof(m_aInput));
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "candidate.bin", m_aCandidate, sizeof(m_aCandidate));
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "asset.bin", m_aOfficial, sizeof(m_aOfficial));
			ASSERT_NO_FATAL_FAILURE(WriteFile(m_aInput, "new asset contents"));
			ASSERT_NO_FATAL_FAILURE(WriteFile(m_aOfficial, "old asset"));
		}
		android_assets::EStage Run(std::int64_t Length)
		{
			std::unique_ptr<void, decltype(&io_close)> Input(io_open(m_aInput, IOFLAG_READ), &io_close);
			std::unique_ptr<void, decltype(&io_close)> Output(io_open(m_aCandidate, IOFLAG_WRITE), &io_close);
			EXPECT_TRUE(Input);
			EXPECT_TRUE(Output);
			if(!Input || !Output)
				return android_assets::EStage::PREPARATION;
			return android_assets::Unpack(Length, [&](void *pData, size_t Size, size_t &Read) {
				Read = io_read(Input.get(), pData, static_cast<unsigned>(Size));
				return io_error(Input.get()) == 0; }, [&](const void *pData, size_t Size) { return io_write(Output.get(), pData, static_cast<unsigned>(Size)) == Size && io_error(Output.get()) == 0; }, [&]() noexcept { return io_close(Input.release()) == 0; }, [&](bool Complete) noexcept {
				const bool Flushed = !Complete || io_flush(Output.get()) == 0;
				const bool Closed = io_close(Output.release()) == 0;
				return Complete && Flushed && Closed && !m_OutputCloseFailure; }, [&] {
				const std::string Target = m_RenameFailure ? std::string(m_aOfficial) + "/child" : m_aOfficial;
				return fs_rename(m_aCandidate, Target.c_str(), false) == 0; });
		}
	};
}

TEST_F(CAndroidAssetUnpackFileTest, CompleteCandidateReplacesExistingAsset)
{
	ASSERT_EQ(Run(18), android_assets::EStage::DONE);
	EXPECT_EQ(ReadFile(m_aOfficial), "new asset contents");
	EXPECT_FALSE(fs_is_file(m_aCandidate));
}

TEST_F(CAndroidAssetUnpackFileTest, EarlyEofRetainsExistingAssetAndPartialCandidate)
{
	ASSERT_EQ(Run(19), android_assets::EStage::READ);
	EXPECT_EQ(ReadFile(m_aOfficial), "old asset");
	EXPECT_EQ(ReadFile(m_aCandidate), "new asset contents");
}

TEST_F(CAndroidAssetUnpackFileTest, FailedCloseFeedbackRetainsExistingAsset)
{
	m_OutputCloseFailure = true;
	ASSERT_EQ(Run(18), android_assets::EStage::OUTPUT_CLOSE);
	EXPECT_EQ(ReadFile(m_aOfficial), "old asset");
	EXPECT_EQ(ReadFile(m_aCandidate), "new asset contents");
}

TEST_F(CAndroidAssetUnpackFileTest, FilesystemRenameFailureRetainsOldAssetAndCandidate)
{
	m_RenameFailure = true;
	ASSERT_EQ(Run(18), android_assets::EStage::PUBLISH);
	EXPECT_EQ(ReadFile(m_aOfficial), "old asset");
	EXPECT_EQ(ReadFile(m_aCandidate), "new asset contents");
}
