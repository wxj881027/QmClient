#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <fstream>

TEST(ConfigMigrationBackup, FinalizePreservesLinkedBackupAndExternalContents)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto Root = std::filesystem::absolute(Info.StoragePath());
	std::filesystem::create_directories(Root / "qmclient");
	std::filesystem::create_directories(Root / "external");
	std::ofstream(Root / "qmclient/settings.cfg") << "qm_fast_input 1\n";
	std::ofstream(Root / "external/keep.txt") << "user data";
	const auto Link = Root / "qmclient/migration_backup_v2";
	std::error_code Error;
	std::filesystem::create_directory_symlink(Root / "external", Link, Error);
	if(Error)
		GTEST_SKIP() << "directory links unavailable: " << Error.message();
	// 必须先移除链接，再让测试目录清理器递归清理隔离数据。
	struct SRemoveLink
	{
		std::filesystem::path m_Path;
		~SRemoveLink()
		{
			std::error_code Ignored;
			std::filesystem::remove(m_Path, Ignored);
		}
	} RemoveLink{Link};
	ASSERT_TRUE(QmFinalizeConfigMigration(pStorage.get()));
	EXPECT_TRUE(std::filesystem::is_symlink(Link));
	std::string Contents;
	std::ifstream Input(Root / "external/keep.txt");
	std::getline(Input, Contents);
	EXPECT_EQ(Contents, "user data");
	EXPECT_EQ(std::filesystem::file_size(Root / "external/keep.txt"), 9u);
	// 重复收尾也不能遍历或删除历史备份。
	EXPECT_TRUE(QmFinalizeConfigMigration(pStorage.get()));
	EXPECT_TRUE(std::filesystem::exists(Root / "external/keep.txt"));
}
