#include <base/fs.h>
#include <base/system.h>

#include <engine/client/steam_client_path.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>

namespace
{
	class CSteamPathFilesystemTest : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		char m_aPath[IO_MAX_PATH_LENGTH];
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "steam-client.exe", m_aPath, sizeof(m_aPath));
		}
		void CreateFile()
		{
			std::unique_ptr<void, decltype(&io_close)> File(io_open(m_aPath, IOFLAG_WRITE), &io_close);
			ASSERT_TRUE(File);
			EXPECT_EQ(io_close(File.release()), 0);
		}
	};
}

TEST_F(CSteamPathFilesystemTest, ExistingFilePublishesTheSamePathThatWasChecked)
{
	ASSERT_NO_FATAL_FAILURE(CreateFile());
	char aOutput[IO_MAX_PATH_LENGTH] = {};
	ASSERT_TRUE(steam_path::Accept(m_aPath, aOutput, sizeof(aOutput), fs_is_file));
	EXPECT_STREQ(aOutput, m_aPath);
	EXPECT_TRUE(fs_is_file(aOutput));
}

TEST_F(CSteamPathFilesystemTest, SmallOutputDoesNotReturnTruncatedExistingFile)
{
	ASSERT_NO_FATAL_FAILURE(CreateFile());
	char aOutput[4] = "old";
	EXPECT_FALSE(steam_path::Accept(m_aPath, aOutput, sizeof(aOutput), fs_is_file));
	EXPECT_STREQ(aOutput, "old");
}

TEST_F(CSteamPathFilesystemTest, MissingFileDoesNotReplacePreviousPath)
{
	char aOutput[IO_MAX_PATH_LENGTH] = "previous";
	EXPECT_FALSE(steam_path::Accept(m_aPath, aOutput, sizeof(aOutput), fs_is_file));
	EXPECT_STREQ(aOutput, "previous");
}
