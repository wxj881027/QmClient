// 商店存储策略与真实隔离 IStorage 协作，验证路径、目录失败及旧字体兼容。
#include <base/system.h>

#include <engine/storage.h>

#include <game/client/components/qmclient/font_download_storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
	class CQmFontDownloadStorage : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void WriteFile(const char *pPath, const char *pText)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			io_write(File, pText, str_length(pText));
			io_close(File);
		}
		std::string ReadFile(const char *pPath)
		{
			char *pText = m_pStorage->ReadFileStr(pPath, IStorage::TYPE_SAVE);
			if(pText == nullptr)
				return {};
			const std::string Text(pText);
			free(pText);
			return Text;
		}
	};
}

TEST_F(CQmFontDownloadStorage, FreshSaveRootCreatesParentsAndUsesDownloadedFonts)
{
	EXPECT_FALSE(m_pStorage->FolderExists("fonts", IStorage::TYPE_SAVE));
	ASSERT_TRUE(qm_font_download::EnsureDirectory(*m_pStorage));
	EXPECT_TRUE(qm_font_download::EnsureDirectory(*m_pStorage));
	char aTarget[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(qm_font_download::TargetPath("用户 Font.ttf", aTarget, sizeof(aTarget)));
	EXPECT_STREQ(aTarget, "fonts/downloaded_fonts/用户 Font.ttf");
	WriteFile(aTarget, "font-bytes");
	EXPECT_TRUE(qm_font_download::Installed(*m_pStorage, "用户 Font.ttf"));
	EXPECT_FALSE(m_pStorage->FolderExists("qmclient/fonts", IStorage::TYPE_SAVE));
	char aDirectory[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(qm_font_download::CompleteDirectoryPath(*m_pStorage, aDirectory, sizeof(aDirectory)));
	EXPECT_EQ(std::filesystem::path(aDirectory), std::filesystem::path(m_Info.StoragePath()) / "fonts" / "downloaded_fonts");
}

TEST_F(CQmFontDownloadStorage, LegacyDownloadsRemainInstalledAndUnknownFilesAreUntouched)
{
	ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
	ASSERT_TRUE(m_pStorage->CreateFolder("qmclient/fonts", IStorage::TYPE_SAVE));
	WriteFile("qmclient/fonts/Old.ttf", "old-font");
	WriteFile("qmclient/fonts/personal-data.bin", "unknown-user-data");
	EXPECT_TRUE(qm_font_download::Installed(*m_pStorage, "Old.ttf"));
	EXPECT_FALSE(qm_font_download::Installed(*m_pStorage, "Missing.ttf"));
	ASSERT_TRUE(qm_font_download::EnsureDirectory(*m_pStorage));
	WriteFile("fonts/downloaded_fonts/Old.ttf", "new-font");
	EXPECT_TRUE(qm_font_download::Installed(*m_pStorage, "Old.ttf"));
	EXPECT_EQ(ReadFile("qmclient/fonts/Old.ttf"), "old-font");
	EXPECT_EQ(ReadFile("fonts/downloaded_fonts/Old.ttf"), "new-font");
	EXPECT_EQ(ReadFile("qmclient/fonts/personal-data.bin"), "unknown-user-data");
}

TEST_F(CQmFontDownloadStorage, ParentFileFailureClearsOpenDirectoryPathAndPreservesFile)
{
	WriteFile("fonts", "user-file");
	EXPECT_FALSE(qm_font_download::EnsureDirectory(*m_pStorage));
	char aDirectory[IO_MAX_PATH_LENGTH] = "stale";
	EXPECT_FALSE(qm_font_download::CompleteDirectoryPath(*m_pStorage, aDirectory, sizeof(aDirectory)));
	EXPECT_STREQ(aDirectory, "");
	EXPECT_EQ(ReadFile("fonts"), "user-file");
}

TEST_F(CQmFontDownloadStorage, RejectsTraversalAndTooSmallDestinationWithoutInstalling)
{
	for(const char *pName : {"../escape.ttf", "nested/a.ttf", "nested\\a.ttf", "C:font.ttf", "font.txt", ""})
	{
		SCOPED_TRACE(pName);
		char aPath[IO_MAX_PATH_LENGTH] = "stale";
		EXPECT_FALSE(qm_font_download::TargetPath(pName, aPath, sizeof(aPath)));
		EXPECT_STREQ(aPath, "");
		EXPECT_FALSE(qm_font_download::Installed(*m_pStorage, pName));
	}
	char aTiny[4] = "old";
	EXPECT_FALSE(qm_font_download::TargetPath("valid.otf", aTiny, sizeof(aTiny)));
	EXPECT_STREQ(aTiny, "");
}
