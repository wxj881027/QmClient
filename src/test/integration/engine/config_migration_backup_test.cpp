#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <fstream>

namespace
{
	// 先移除测试创建的链接，再由测试目录清理器处理隔离数据。
	struct SRemoveLink
	{
		std::filesystem::path m_Path;
		~SRemoveLink()
		{
			std::error_code Ignored;
			std::filesystem::remove(m_Path, Ignored);
		}
	};
}

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
	SRemoveLink RemoveLink{Link};
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

#if !defined(CONF_FAMILY_WINDOWS) && !defined(CONF_PLATFORM_MACOS)
TEST(ConfigMigrationBackup, FinalizeKeepsCurrentProfilesWhenLegacyDirectoryIsAnAlias)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("QmClient", IStorage::TYPE_SAVE));
	const auto Legacy = std::filesystem::absolute(Info.StoragePath()) / "QmClient";
	const auto Current = Legacy.parent_path() / "qmclient";
	std::error_code Error;
	std::filesystem::create_directory_symlink(Legacy, Current, Error);
	if(Error)
		GTEST_SKIP() << "directory alias unavailable: " << Error.message();
	SRemoveLink RemoveLink{Current};
	std::ofstream(Current / "settings.cfg") << "qm_fast_input 1\n";
	std::ofstream(Current / "qmclient_profiles.cfg") << "# current profile\n";

	ASSERT_TRUE(QmFinalizeConfigMigration(pStorage.get()));
	std::string Contents;
	std::ifstream Input(Current / "qmclient_profiles.cfg");
	ASSERT_TRUE(std::getline(Input, Contents));
	EXPECT_EQ(Contents, "# current profile");
}
#endif
