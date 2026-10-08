#include <engine/storage.h>

#include <game/client/components/qmclient/config_package.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <filesystem>
#include <memory>
#include <string>

namespace
{
	class CQmConfigPackageStorage : public testing::Test
	{
	protected:
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_pFiles = std::make_unique<qm_config_package::CStorageFiles>(*m_pStorage);
			for(const char *pPath : {"qmclient/settings.cfg", "qmclient/qmclient_profiles.cfg", "qmclient/qmclient_chatbinds.cfg", "qmclient/qmclient_warlist.cfg"})
				ASSERT_TRUE(m_pFiles->Write(pPath, std::string("old:") + pPath + "\r\n"));
			std::string Error;
			ASSERT_TRUE(qm_config_package::Collect(*m_pFiles, {}, "3.3", "2026-10-09T08:00:00Z", m_Incoming, Error)) << Error;
			for(auto &File : m_Incoming.m_vFiles)
				File.m_Content = "new:" + File.m_Path + "\n";
		}

		std::string Read(const std::string &Path)
		{
			std::string Result;
			EXPECT_TRUE(m_pFiles->Read(Path, Result, qm_config_package::MAX_PACKAGE_BYTES)) << Path;
			return Result;
		}

		void ExpectOriginalManagedFiles()
		{
			for(size_t Index = 0; Index < 4; ++Index)
			{
				const auto &Path = m_Incoming.m_vFiles[Index].m_Path;
				EXPECT_EQ(Read(Path), "old:" + Path + "\r\n");
			}
		}

		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<qm_config_package::CStorageFiles> m_pFiles;
		qm_config_package::SPackage m_Incoming;
		const std::string m_BackupPath = "qmclient/config_packages/backups/before.qmconfig";
	};

	// 故障仅替换文件操作边界，备份、暂存、提交与回滚均调用同一生产恢复实现。
	class CFailPromotion final : public qm_config_package::IFileStore
	{
		qm_config_package::IFileStore &m_Files;
		std::string m_Target;
		bool m_Failed = false;

	public:
		CFailPromotion(qm_config_package::IFileStore &Files, std::string Target) :
			m_Files(Files), m_Target(std::move(Target)) {}
		bool Read(const std::string &Path, std::string &Content, size_t MaxBytes) override { return m_Files.Read(Path, Content, MaxBytes); }
		bool Exists(const std::string &Path) override { return m_Files.Exists(Path); }
		bool Write(const std::string &Path, std::string_view Content) override { return m_Files.Write(Path, Content); }
		bool Remove(const std::string &Path) override { return m_Files.Remove(Path); }
		bool Rename(const std::string &From, const std::string &To) override
		{
			if(!m_Failed && To == m_Target && From.ends_with(".new"))
			{
				m_Failed = true;
				return false;
			}
			return m_Files.Rename(From, To);
		}
	};
}

TEST_F(CQmConfigPackageStorage, ExportIncludesOnlySelectedCustomFilesAndPreservesTheirBytes)
{
	ASSERT_TRUE(m_pFiles->Write("scripts/sub/action.cfg", "echo \"中文\"\r\n"));
	ASSERT_TRUE(m_pFiles->Write("scripts/unselected.cfg", "echo private\n"));
	qm_config_package::SPackage Exported;
	std::string Error;
	ASSERT_TRUE(qm_config_package::Collect(*m_pFiles, {"scripts/sub/action.cfg"}, "3.3", "2026-10-09T08:00:00Z", Exported, Error)) << Error;
	ASSERT_EQ(Exported.m_vFiles.size(), 5u);
	EXPECT_EQ(Exported.m_vFiles.back().m_Path, "scripts/sub/action.cfg");
	EXPECT_EQ(Exported.m_vFiles.back().m_Content, "echo \"中文\"\r\n");
	ASSERT_TRUE(qm_config_package::Save(*m_pFiles, "qmclient/config_packages/export.qmconfig", Exported, Error));
	qm_config_package::SPackage Loaded;
	ASSERT_TRUE(qm_config_package::Load(*m_pFiles, "qmclient/config_packages/export.qmconfig", Loaded, Error));
	EXPECT_EQ(Loaded.m_vFiles.size(), 5u);
	EXPECT_EQ(Loaded.m_vFiles.back().m_Content, Exported.m_vFiles.back().m_Content);
}

TEST_F(CQmConfigPackageStorage, RestoreBacksUpPreviousFilesAndKeepsUnlistedCustomFiles)
{
	ASSERT_TRUE(m_pFiles->Write("scripts/action.cfg", "echo old\n"));
	ASSERT_TRUE(m_pFiles->Write("scripts/untouched.cfg", "echo untouched\n"));
	m_Incoming.m_vFiles.push_back({"scripts/action.cfg", "echo new\r\n"});
	std::string Error;
	ASSERT_TRUE(qm_config_package::Restore(*m_pFiles, m_Incoming, m_BackupPath, Error)) << Error;
	for(const auto &File : m_Incoming.m_vFiles)
		EXPECT_EQ(Read(File.m_Path), File.m_Content);
	EXPECT_EQ(Read("scripts/untouched.cfg"), "echo untouched\n");
	qm_config_package::SPackage Backup;
	ASSERT_TRUE(qm_config_package::Load(*m_pFiles, m_BackupPath, Backup, Error));
	EXPECT_TRUE(Backup.m_Backup);
	EXPECT_EQ(Backup.m_vFiles[0].m_Content, "old:qmclient/settings.cfg\r\n");
	EXPECT_EQ(Backup.m_vFiles.back().m_Content, "echo old\n");
}

TEST_F(CQmConfigPackageStorage, BackupFailurePreventsAllTargetChanges)
{
	ASSERT_TRUE(m_pFiles->Write(m_BackupPath, "occupied"));
	std::string Error;
	EXPECT_FALSE(qm_config_package::Restore(*m_pFiles, m_Incoming, m_BackupPath, Error));
	ExpectOriginalManagedFiles();
	EXPECT_EQ(Read(m_BackupPath), "occupied");
}

TEST_F(CQmConfigPackageStorage, LatePromotionFailureRollsBackPreviouslyReplacedFiles)
{
	m_Incoming.m_vFiles.push_back({"scripts/new.cfg", "echo new\n"});
	CFailPromotion Fault(*m_pFiles, m_Incoming.m_vFiles[3].m_Path);
	std::string Error;
	EXPECT_FALSE(qm_config_package::Restore(Fault, m_Incoming, m_BackupPath, Error));
	ExpectOriginalManagedFiles();
	EXPECT_FALSE(m_pFiles->Exists("scripts/new.cfg"));
	qm_config_package::SPackage Backup;
	EXPECT_TRUE(qm_config_package::Load(*m_pFiles, m_BackupPath, Backup, Error));
}

TEST_F(CQmConfigPackageStorage, StagingFailureLeavesEveryTargetUntouchedAndKeepsTheBackup)
{
	ASSERT_TRUE(m_pFiles->Write("scripts/blocked", "regular file"));
	m_Incoming.m_vFiles.push_back({"scripts/blocked/action.cfg", "echo cannot stage"});
	std::string Error;
	EXPECT_FALSE(qm_config_package::Restore(*m_pFiles, m_Incoming, m_BackupPath, Error));
	ExpectOriginalManagedFiles();
	EXPECT_EQ(Read("scripts/blocked"), "regular file");
	EXPECT_TRUE(m_pFiles->Exists(m_BackupPath));
}

TEST_F(CQmConfigPackageStorage, RollbackRemovesANewlyCreatedCustomFile)
{
	ASSERT_TRUE(m_pFiles->Write("scripts/last.cfg", "old last"));
	m_Incoming.m_vFiles.push_back({"scripts/new.cfg", "new custom"});
	m_Incoming.m_vFiles.push_back({"scripts/last.cfg", "new last"});
	CFailPromotion Fault(*m_pFiles, "scripts/last.cfg");
	std::string Error;
	EXPECT_FALSE(qm_config_package::Restore(Fault, m_Incoming, m_BackupPath, Error));
	ExpectOriginalManagedFiles();
	EXPECT_FALSE(m_pFiles->Exists("scripts/new.cfg"));
	EXPECT_EQ(Read("scripts/last.cfg"), "old last");
}

TEST_F(CQmConfigPackageStorage, ImportingRecoveryBackupRemovesAFileThatDidNotPreviouslyExist)
{
	m_Incoming.m_vFiles.push_back({"scripts/new.cfg", "echo new\n"});
	std::string Error;
	ASSERT_TRUE(qm_config_package::Restore(*m_pFiles, m_Incoming, m_BackupPath, Error)) << Error;
	EXPECT_TRUE(m_pFiles->Exists("scripts/new.cfg"));
	qm_config_package::SPackage Backup;
	ASSERT_TRUE(qm_config_package::Load(*m_pFiles, m_BackupPath, Backup, Error));
	ASSERT_TRUE(qm_config_package::Restore(*m_pFiles, Backup, "qmclient/config_packages/backups/undo.qmconfig", Error)) << Error;
	ExpectOriginalManagedFiles();
	EXPECT_FALSE(m_pFiles->Exists("scripts/new.cfg"));
}

TEST_F(CQmConfigPackageStorage, ListCustomConfigsExcludesManagedFilesPackagesAndBuiltInScripts)
{
	ASSERT_TRUE(m_pFiles->Write("scripts/sub/action.cfg", "echo custom"));
	ASSERT_TRUE(m_pFiles->Write("autoexec_client.cfg", "exec scripts/sub/action.cfg"));
	ASSERT_TRUE(m_pFiles->Write("settings_ddnet.cfg", "official"));
	ASSERT_TRUE(m_pFiles->Write("qmclient/builtinscripts/action.cfg", "built in"));
	std::vector<std::string> vFiles;
	std::string Error;
	ASSERT_TRUE(qm_config_package::ListCustomConfigs(*m_pStorage, vFiles, Error)) << Error;
	ASSERT_EQ(vFiles.size(), 2u);
	EXPECT_EQ(vFiles[0], "autoexec_client.cfg");
	EXPECT_EQ(vFiles[1], "scripts/sub/action.cfg");
}

TEST_F(CQmConfigPackageStorage, SymbolicLinkCannotReadOrOverwriteAnAliasedConfiguration)
{
	ASSERT_TRUE(m_pFiles->Write("safe/target.cfg", "preserve"));
	char aRoot[IO_MAX_PATH_LENGTH];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
	const auto Root = std::filesystem::u8path(aRoot);
	std::error_code Ec;
	std::filesystem::create_directory_symlink(Root / "safe", Root / "linked", Ec);
	if(Ec)
		GTEST_SKIP() << "Symbolic link creation is unavailable: " << Ec.message();
	std::string Content;
	EXPECT_FALSE(m_pFiles->Read("linked/target.cfg", Content, 100));
	EXPECT_FALSE(m_pFiles->Write("linked/new.cfg", "blocked"));
	EXPECT_EQ(Read("safe/target.cfg"), "preserve");
	EXPECT_FALSE(m_pFiles->Exists("safe/new.cfg"));
}
