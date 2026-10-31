#include <engine/client/updater_file.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{
	class CControlledFiles : public QmLegacyUpdater::CStorageFiles
	{
	public:
		using CStorageFiles::CStorageFiles;
		int m_Renames = 0;
		std::vector<int> m_FailedRenames;
		int m_ThrowAtRename = 0;
		bool m_RemoveFails = false;

		bool Rename(const char *pOld, const char *pNew)
		{
			++m_Renames;
			if(m_Renames == m_ThrowAtRename)
				throw 1;
			for(int Failure : m_FailedRenames)
				if(Failure == m_Renames)
					return false;
			return CStorageFiles::Rename(pOld, pNew);
		}
		bool Remove(const char *pPath) { return !m_RemoveFails && CStorageFiles::Remove(pPath); }
	};

	class LegacyUpdaterFiles : public testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		std::string m_Candidate, m_Destination, m_Backup;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			m_Candidate = Path("candidate.bin");
			m_Destination = Path("installed.bin");
			m_Backup = Path("installed.old");
			Write(m_Candidate, "new content");
			Write(m_Destination, "old content");
		}
		std::string Path(const char *pName)
		{
			std::array<char, IO_MAX_PATH_LENGTH> aPath{};
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pName, aPath.data(), aPath.size());
			return aPath.data();
		}
		void Write(const std::string &Path, const char *pText)
		{
			IOHANDLE File = m_pStorage->OpenFile(Path.c_str(), IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, pText, str_length(pText)), static_cast<unsigned>(str_length(pText)));
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Read(const std::string &Path)
		{
			void *pData = nullptr;
			unsigned Length = 0;
			const bool Success = m_pStorage->ReadFile(Path.c_str(), IStorage::TYPE_ABSOLUTE, &pData, &Length);
			std::unique_ptr<void, decltype(&free)> Content(pData, free);
			EXPECT_TRUE(Success);
			return Success ? std::string(static_cast<const char *>(pData), Length) : std::string();
		}
		bool Exists(const std::string &Path) { return m_pStorage->FileExists(Path.c_str(), IStorage::TYPE_ABSOLUTE); }
		bool Replace(CControlledFiles &Files)
		{
			return QmLegacyUpdater::ReplaceFile(Files, m_Candidate.c_str(), m_Destination.c_str(), m_Backup.c_str());
		}
	};
}

TEST_F(LegacyUpdaterFiles, SuccessPublishesTheCandidateAndRemovesTheBackup)
{
	CControlledFiles Files(*m_pStorage);
	ASSERT_TRUE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "new content");
	EXPECT_FALSE(Exists(m_Candidate));
	EXPECT_FALSE(Exists(m_Backup));
}

TEST_F(LegacyUpdaterFiles, MissingCandidateDoesNotMoveTheInstalledFile)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(m_Candidate.c_str(), IStorage::TYPE_ABSOLUTE));
	CControlledFiles Files(*m_pStorage);
	EXPECT_FALSE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "old content");
	EXPECT_FALSE(Exists(m_Backup));
	EXPECT_EQ(Files.m_Renames, 0);
}

TEST_F(LegacyUpdaterFiles, ExistingRecoveryCopyIsNeverOverwritten)
{
	Write(m_Backup, "previous recovery");
	CControlledFiles Files(*m_pStorage);
	EXPECT_FALSE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
	EXPECT_EQ(Read(m_Backup), "previous recovery");
}

TEST_F(LegacyUpdaterFiles, BackupRenameFailureDoesNotInstallTheCandidate)
{
	CControlledFiles Files(*m_pStorage);
	Files.m_FailedRenames = {1};
	EXPECT_FALSE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
	EXPECT_FALSE(Exists(m_Backup));
}

TEST_F(LegacyUpdaterFiles, InstallRenameFailureRestoresTheOldFile)
{
	CControlledFiles Files(*m_pStorage);
	Files.m_FailedRenames = {2};
	EXPECT_FALSE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
	EXPECT_FALSE(Exists(m_Backup));
	Files.m_FailedRenames.clear();
	ASSERT_TRUE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "new content");
}

TEST_F(LegacyUpdaterFiles, FailedRollbackRetainsTheBackupAndCandidate)
{
	CControlledFiles Files(*m_pStorage);
	Files.m_FailedRenames = {2, 3};
	EXPECT_FALSE(Replace(Files));
	EXPECT_FALSE(Exists(m_Destination));
	EXPECT_EQ(Read(m_Backup), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
}

TEST_F(LegacyUpdaterFiles, ExceptionDuringInstallationRestoresTheOldFile)
{
	CControlledFiles Files(*m_pStorage);
	Files.m_ThrowAtRename = 2;
	EXPECT_FALSE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
}

TEST_F(LegacyUpdaterFiles, ExceptionDuringRollbackRetainsTheRecoveryCopy)
{
	CControlledFiles Files(*m_pStorage);
	Files.m_FailedRenames = {2};
	Files.m_ThrowAtRename = 3;
	EXPECT_FALSE(Replace(Files));
	EXPECT_FALSE(Exists(m_Destination));
	EXPECT_EQ(Read(m_Backup), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
}

TEST_F(LegacyUpdaterFiles, CleanupFailureLeavesTheNewFileAndRecoveryCopy)
{
	CControlledFiles Files(*m_pStorage);
	Files.m_RemoveFails = true;
	ASSERT_TRUE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "new content");
	EXPECT_EQ(Read(m_Backup), "old content");
}

TEST_F(LegacyUpdaterFiles, MissingDestinationCanBeInstalledWithoutABackup)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(m_Destination.c_str(), IStorage::TYPE_ABSOLUTE));
	CControlledFiles Files(*m_pStorage);
	ASSERT_TRUE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "new content");
	EXPECT_FALSE(Exists(m_Backup));
}

TEST_F(LegacyUpdaterFiles, BackupDirectoryDoesNotGetReplacedByTheOldFile)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(m_Backup.c_str(), IStorage::TYPE_ABSOLUTE));
	CControlledFiles Files(*m_pStorage);
	EXPECT_FALSE(Replace(Files));
	EXPECT_EQ(Read(m_Destination), "old content");
	EXPECT_EQ(Read(m_Candidate), "new content");
	EXPECT_TRUE(m_pStorage->FolderExists(m_Backup.c_str(), IStorage::TYPE_ABSOLUTE));
}

TEST_F(LegacyUpdaterFiles, DestinationDirectoryDoesNotGetMovedToTheBackup)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(m_Destination.c_str(), IStorage::TYPE_ABSOLUTE));
	ASSERT_TRUE(m_pStorage->CreateFolder(m_Destination.c_str(), IStorage::TYPE_ABSOLUTE));
	CControlledFiles Files(*m_pStorage);
	EXPECT_FALSE(Replace(Files));
	EXPECT_TRUE(m_pStorage->FolderExists(m_Destination.c_str(), IStorage::TYPE_ABSOLUTE));
	EXPECT_EQ(Read(m_Candidate), "new content");
	EXPECT_FALSE(Exists(m_Backup));
}
