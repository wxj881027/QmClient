#include "test.h"

#include <engine/config.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
	void WriteStorageFile(IStorage *pStorage, const char *pFilename, const char *pContent)
	{
		IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_TRUE(File);
		ASSERT_EQ(io_write(File, pContent, str_length(pContent)), str_length(pContent));
		ASSERT_FALSE(io_close(File));
	}

	std::string ReadStorageFile(IStorage *pStorage, const char *pFilename)
	{
		IOHANDLE File = pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_SAVE);
		EXPECT_TRUE(File);
		if(!File)
			return {};
		const int64_t Length = io_length(File);
		std::string Content((size_t)Length, '\0');
		EXPECT_EQ(io_read(File, Content.data(), Content.size()), Length);
		EXPECT_FALSE(io_close(File));
		return Content;
	}
}

TEST(StorageReplace, PromotesTempFileAndRemovesBackup)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteStorageFile(pStorage.get(), "real.txt", "old");
	WriteStorageFile(pStorage.get(), "temp.txt", "new");

	char aBackup[IO_MAX_PATH_LENGTH];
	ASSERT_TRUE(IStorage::ReplaceFileSafely(pStorage.get(), "temp.txt", "real.txt", aBackup, sizeof(aBackup)));
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "real.txt"), "new");
	EXPECT_FALSE(pStorage->FileExists("temp.txt", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists(aBackup, IStorage::TYPE_SAVE));
}

TEST(StorageReplace, RestoresPreviousFileWhenTempPromotionFails)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteStorageFile(pStorage.get(), "real.txt", "old");

	char aBackup[IO_MAX_PATH_LENGTH];
	EXPECT_FALSE(IStorage::ReplaceFileSafely(pStorage.get(), "missing.txt", "real.txt", aBackup, sizeof(aBackup)));
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "real.txt"), "old");
	EXPECT_FALSE(pStorage->FileExists(aBackup, IStorage::TYPE_SAVE));
}

TEST(StorageReplace, ExistingBackupDoesNotDamagePreviousFile)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	WriteStorageFile(pStorage.get(), "real.txt", "old");
	WriteStorageFile(pStorage.get(), "temp.txt", "new");
	WriteStorageFile(pStorage.get(), "temp.txt.backup", "occupied");

	char aBackup[IO_MAX_PATH_LENGTH];
	EXPECT_FALSE(IStorage::ReplaceFileSafely(pStorage.get(), "temp.txt", "real.txt", aBackup, sizeof(aBackup)));
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "real.txt"), "old");
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "temp.txt"), "new");
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "temp.txt.backup"), "occupied");
}

TEST(ConfigMigrationV3, PendingOnlyUntilMergedFileExists)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	// 无 qmclient/settings.cfg → 迁移挂起
	EXPECT_TRUE(QmConfigMigrationPending(pStorage.get()));

	// 写入合并文件 → 迁移完成
	WriteStorageFile(pStorage.get(), "qmclient/settings.cfg", "qm_fast_input 1\n");
	EXPECT_FALSE(QmConfigMigrationPending(pStorage.get()));

	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFolder("qmclient", IStorage::TYPE_SAVE));
}

TEST(ConfigMigrationV3, LoadPathsPreferMergedFile)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	// 只有合并文件 → 只读它
	WriteStorageFile(pStorage.get(), "qmclient/settings.cfg", "qm_fast_input 1\n");
	WriteStorageFile(pStorage.get(), "qmclient/settings_qmclient.cfg", "qm_say_nopop 1\n");
	WriteStorageFile(pStorage.get(), "qmclient/settings_ddnet.cfg", "bind x say legacy\n");

	std::vector<const char *> vPaths;
	QmGetVariableConfigLoadPaths(pStorage.get(), vPaths);
	ASSERT_EQ(vPaths.size(), 1u);
	EXPECT_STREQ(vPaths[0], "qmclient/settings.cfg");

	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings_qmclient.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings_ddnet.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFolder("qmclient", IStorage::TYPE_SAVE));
}

TEST(ConfigMigrationV3, LoadPathsFallbackV2)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	// v2：qmclient/ 下两个旧文件合并读取
	WriteStorageFile(pStorage.get(), "qmclient/settings_qmclient.cfg", "qm_say_nopop 1\n");
	WriteStorageFile(pStorage.get(), "qmclient/settings_ddnet.cfg", "bind x say legacy\n");

	std::vector<const char *> vPaths;
	QmGetVariableConfigLoadPaths(pStorage.get(), vPaths);
	ASSERT_EQ(vPaths.size(), 2u);
	EXPECT_STREQ(vPaths[0], "qmclient/settings_ddnet.cfg");
	EXPECT_STREQ(vPaths[1], "qmclient/settings_qmclient.cfg");

	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings_qmclient.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings_ddnet.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFolder("qmclient", IStorage::TYPE_SAVE));
}

TEST(ConfigMigrationV3, LoadPathsFallbackV1)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	// v1：大写 QmClient/ 目录 + 根目录官方共享。
	// 注意：Windows 文件系统大小写不敏感，QmClient/ 与 qmclient/ 是同一物理目录，
	// v1 文件会被 v2 分支命中；此时官方配置仍在根目录，v2 分支会补读它。
	ASSERT_TRUE(pStorage->CreateFolder("QmClient", IStorage::TYPE_SAVE));
	WriteStorageFile(pStorage.get(), "QmClient/settings_qmclient.cfg", "qm_say_nopop 1\n");
	WriteStorageFile(pStorage.get(), "settings_ddnet.cfg", "bind x say official\n");
	EXPECT_TRUE(pStorage->FileExists("QmClient/settings_qmclient.cfg", IStorage::TYPE_SAVE));
	EXPECT_TRUE(pStorage->FileExists("settings_ddnet.cfg", IStorage::TYPE_SAVE));

	std::vector<const char *> vPaths;
	QmGetVariableConfigLoadPaths(pStorage.get(), vPaths);
	ASSERT_EQ(vPaths.size(), 2u);
	// 第一个是 settings_qmclient.cfg（Windows 上折叠为 qmclient/ 路径，大小写敏感平台为大写 QmClient/）
	EXPECT_TRUE(str_endswith(vPaths[0], "settings_qmclient.cfg"));
	EXPECT_STREQ(vPaths[1], "settings_ddnet.cfg");

	ASSERT_TRUE(pStorage->RemoveFile("QmClient/settings_qmclient.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("settings_ddnet.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFolder("QmClient", IStorage::TYPE_SAVE));
}

TEST(ConfigMigrationV3, LoadPathsFallbackV0AndFresh)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);

	// v0：根目录散落文件
	WriteStorageFile(pStorage.get(), "settings_ddnet.cfg", "bind x say official\n");
	WriteStorageFile(pStorage.get(), "settings_qmclient.cfg", "qm_fast_input 0\n");

	std::vector<const char *> vPaths;
	QmGetVariableConfigLoadPaths(pStorage.get(), vPaths);
	ASSERT_EQ(vPaths.size(), 2u);
	EXPECT_STREQ(vPaths[0], "settings_ddnet.cfg");
	EXPECT_STREQ(vPaths[1], "settings_qmclient.cfg");

	ASSERT_TRUE(pStorage->RemoveFile("settings_ddnet.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("settings_qmclient.cfg", IStorage::TYPE_SAVE));

	// 全新用户：无任何文件 → 空列表
	QmGetVariableConfigLoadPaths(pStorage.get(), vPaths);
	EXPECT_TRUE(vPaths.empty());
}

TEST(ConfigMigrationV3, FinalizeKeepsOfficialDdnetAndCleansEverythingElse)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	// 合并文件已存在（模拟 Save 完成）
	WriteStorageFile(pStorage.get(), "qmclient/settings.cfg", "qm_fast_input 1\n");

	// v2 残留
	WriteStorageFile(pStorage.get(), "qmclient/settings_qmclient.cfg", "qm_say_nopop 1\n");
	WriteStorageFile(pStorage.get(), "qmclient/settings_ddnet.cfg", "bind x say legacy\n");
	WriteStorageFile(pStorage.get(), "qmclient/config_migration_v2.done", "1\n");
	ASSERT_TRUE(pStorage->CreateFolder("qmclient/migration_backup_v1", IStorage::TYPE_SAVE));
	WriteStorageFile(pStorage.get(), "qmclient/migration_backup_v1/settings_ddnet.cfg", "backup v1\n");
	ASSERT_TRUE(pStorage->CreateFolder("qmclient/migration_backup_v2", IStorage::TYPE_SAVE));
	WriteStorageFile(pStorage.get(), "qmclient/migration_backup_v2/settings_qmclient.cfg", "backup v2\n");

	// v1 目录
	ASSERT_TRUE(pStorage->CreateFolder("QmClient", IStorage::TYPE_SAVE));
	WriteStorageFile(pStorage.get(), "QmClient/settings_qmclient.cfg", "qm_say_nopop 1\n");
	WriteStorageFile(pStorage.get(), "QmClient/qmclient_profiles.cfg", "profile legacy\n");

	// v0 根目录散落（settings_ddnet.cfg 是官方共享，必须保留）
	WriteStorageFile(pStorage.get(), "settings_ddnet.cfg", "bind x say official\n");
	WriteStorageFile(pStorage.get(), "settings_qmclient.cfg", "qm_fast_input 0\n");
	WriteStorageFile(pStorage.get(), "qmclient_profiles.cfg", "profile legacy\n");
	WriteStorageFile(pStorage.get(), "qmclient_chatbinds.cfg", "chatbind legacy\n");
	WriteStorageFile(pStorage.get(), "qmclient_warlist.cfg", "warlist legacy\n");

	ASSERT_TRUE(QmFinalizeConfigMigration(pStorage.get()));

	// 合并文件与官方共享配置保留
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/settings.cfg"), "qm_fast_input 1\n");
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "settings_ddnet.cfg"), "bind x say official\n");

	// v2 残留清理
	EXPECT_FALSE(pStorage->FileExists("qmclient/settings_qmclient.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient/settings_ddnet.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient/config_migration_v2.done", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient/migration_backup_v1", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient/migration_backup_v2", IStorage::TYPE_SAVE));

	// v1 目录清理
	EXPECT_FALSE(pStorage->FileExists("QmClient", IStorage::TYPE_SAVE));

	// v0 根目录 qm 专属文件清理
	EXPECT_FALSE(pStorage->FileExists("settings_qmclient.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient_profiles.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient_chatbinds.cfg", IStorage::TYPE_SAVE));
	EXPECT_FALSE(pStorage->FileExists("qmclient_warlist.cfg", IStorage::TYPE_SAVE));

	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings.cfg", IStorage::TYPE_SAVE));
	// Windows 上 QmClient/qmclient_profiles.cfg 与 qmclient/qmclient_profiles.cfg 是同一文件，
	// 迁移逻辑会保留它（无变量域当前文件），测试清理时需要一并删除。
	ASSERT_TRUE(pStorage->RemoveFile("qmclient/qmclient_profiles.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("settings_ddnet.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFolder("qmclient", IStorage::TYPE_SAVE));
}

TEST(ConfigMigrationV3, FinalizeRefusesWithoutMergedFile)
{
	CTestInfo Info;
	std::unique_ptr<IStorage> pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));

	// 没有合并文件（Save 未成功）→ 不清理，避免误删用户数据
	WriteStorageFile(pStorage.get(), "qmclient/settings_qmclient.cfg", "qm_say_nopop 1\n");
	WriteStorageFile(pStorage.get(), "settings_qmclient.cfg", "qm_fast_input 0\n");

	EXPECT_FALSE(QmFinalizeConfigMigration(pStorage.get()));
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "qmclient/settings_qmclient.cfg"), "qm_say_nopop 1\n");
	EXPECT_EQ(ReadStorageFile(pStorage.get(), "settings_qmclient.cfg"), "qm_fast_input 0\n");

	ASSERT_TRUE(pStorage->RemoveFile("qmclient/settings_qmclient.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFile("settings_qmclient.cfg", IStorage::TYPE_SAVE));
	ASSERT_TRUE(pStorage->RemoveFolder("qmclient", IStorage::TYPE_SAVE));
}
