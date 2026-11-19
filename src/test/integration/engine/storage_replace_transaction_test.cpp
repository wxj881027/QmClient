#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <string>
#include <string_view>

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

namespace
{
	constexpr char TARGET[] = "real.txt";
	constexpr char CANDIDATE[] = "temp.txt";
	constexpr char BACKUP[] = "temp.txt.backup";
	enum class ERenameResult
	{
		NATIVE,
		REJECT,
		BAD_ALLOC,
		OTHER_EXCEPTION,
		MOVE_THEN_THROW,
	};

	// 只替换存储边界；事务状态和回滚均由真实IStorage入口执行。
	class CReplacementStorage final : public IStorage
	{
		IStorage &m_Storage;
		bool RenameWithResult(ERenameResult Result, const char *pOld, const char *pNew, int Type, bool AllowRemoval)
		{
			if(Result == ERenameResult::REJECT)
				return false;
			if(Result == ERenameResult::BAD_ALLOC)
				throw std::bad_alloc();
			if(Result == ERenameResult::OTHER_EXCEPTION)
				throw 7;
			const bool Moved = m_Storage.RenameFile(pOld, pNew, Type, AllowRemoval);
			if(Moved && Result == ERenameResult::MOVE_THEN_THROW)
				throw std::bad_alloc();
			return Moved;
		}

	public:
		ERenameResult m_StageResult = ERenameResult::NATIVE;
		ERenameResult m_PromotionResult = ERenameResult::NATIVE;
		ERenameResult m_RollbackResult = ERenameResult::NATIVE;
		bool m_ThrowInitialLookup = false;
		bool m_ThrowBackupLookup = false;
		bool m_ThrowRollbackLookup = false;
		bool m_ThrowCleanup = false;
		bool m_FailCleanup = false;
		bool m_ConcurrentTarget = false;
		bool m_StageAttempted = false;
		bool m_UsedDestinationRemoval = false;
		int m_Renames = 0;
		explicit CReplacementStorage(IStorage &Storage) :
			m_Storage(Storage) {}
		int NumPaths() const override { return m_Storage.NumPaths(); }
		bool GetDataPath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetDataPath(pName, pBuffer, Size); }
		bool GetExecutablePath(const char *pName, char *pBuffer, unsigned Size) const override { return m_Storage.GetExecutablePath(pName, pBuffer, Size); }
		void ListDirectory(int Type, const char *pPath, FS_LISTDIR_CALLBACK Callback, void *pUser) override { m_Storage.ListDirectory(Type, pPath, Callback, pUser); }
		void ListDirectoryInfo(int Type, const char *pPath, FS_LISTDIR_CALLBACK_FILEINFO Callback, void *pUser) override { m_Storage.ListDirectoryInfo(Type, pPath, Callback, pUser); }
		IOHANDLE OpenFile(const char *pName, int Flags, int Type, char *pBuffer, int Size) override { return m_Storage.OpenFile(pName, Flags, Type, pBuffer, Size); }
		bool FileExists(const char *pName, int Type) override
		{
			if(std::string_view(pName) == TARGET && ((!m_StageAttempted && m_ThrowInitialLookup) || (m_StageAttempted && m_ThrowRollbackLookup)))
				throw std::bad_alloc();
			if(std::string_view(pName) == BACKUP && !m_StageAttempted && m_ThrowBackupLookup)
				throw std::bad_alloc();
			return m_Storage.FileExists(pName, Type);
		}
		bool FolderExists(const char *pName, int Type) override { return m_Storage.FolderExists(pName, Type); }
		bool ReadFile(const char *pName, int Type, void **ppData, unsigned *pSize) override { return m_Storage.ReadFile(pName, Type, ppData, pSize); }
		char *ReadFileStr(const char *pName, int Type) override { return m_Storage.ReadFileStr(pName, Type); }
		bool RetrieveTimes(const char *pName, int Type, time_t *pCreated, time_t *pModified) override { return m_Storage.RetrieveTimes(pName, Type, pCreated, pModified); }
		bool CalculateHashes(const char *pName, int Type, SHA256_DIGEST *pSha, unsigned *pCrc) override { return m_Storage.CalculateHashes(pName, Type, pSha, pCrc); }
		bool FindFile(const char *pName, const char *pPath, int Type, char *pBuffer, int Size) override { return m_Storage.FindFile(pName, pPath, Type, pBuffer, Size); }
		size_t FindFiles(const char *pName, const char *pPath, int Type, std::set<std::string> *pEntries) override { return m_Storage.FindFiles(pName, pPath, Type, pEntries); }
		bool RemoveFile(const char *pName, int Type) override
		{
			if(std::string_view(pName) == BACKUP)
			{
				if(m_ThrowCleanup)
					throw std::bad_alloc();
				if(m_FailCleanup)
					return false;
			}
			return m_Storage.RemoveFile(pName, Type);
		}
		bool RemoveFolder(const char *pName, int Type) override { return m_Storage.RemoveFolder(pName, Type); }
		bool RenameFile(const char *pOld, const char *pNew, int Type, bool AllowRemoval) override
		{
			++m_Renames;
			m_UsedDestinationRemoval |= AllowRemoval;
			if(std::string_view(pOld) == TARGET && std::string_view(pNew) == BACKUP)
			{
				m_StageAttempted = true;
				return RenameWithResult(m_StageResult, pOld, pNew, Type, AllowRemoval);
			}
			if(std::string_view(pOld) == CANDIDATE && std::string_view(pNew) == TARGET)
			{
				if(m_ConcurrentTarget)
					WriteStorageFile(&m_Storage, TARGET, "concurrent");
				return RenameWithResult(m_PromotionResult, pOld, pNew, Type, AllowRemoval);
			}
			if(std::string_view(pOld) == BACKUP && std::string_view(pNew) == TARGET)
				return RenameWithResult(m_RollbackResult, pOld, pNew, Type, AllowRemoval);
			return m_Storage.RenameFile(pOld, pNew, Type, AllowRemoval);
		}
		bool CreateFolder(const char *pName, int Type) override { return m_Storage.CreateFolder(pName, Type); }
		void GetCompletePath(int Type, const char *pName, char *pBuffer, unsigned Size) override { m_Storage.GetCompletePath(Type, pName, pBuffer, Size); }
		bool RemoveBinaryFile(const char *pName) override { return m_Storage.RemoveBinaryFile(pName); }
		bool RenameBinaryFile(const char *pOld, const char *pNew) override { return m_Storage.RenameBinaryFile(pOld, pNew); }
		const char *GetBinaryPath(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPath(pName, pBuffer, Size); }
		const char *GetBinaryPathAbsolute(const char *pName, char *pBuffer, unsigned Size) override { return m_Storage.GetBinaryPathAbsolute(pName, pBuffer, Size); }
	};

	class StorageReplaceTransaction : public ::testing::Test
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		std::unique_ptr<CReplacementStorage> m_pFaults;
		char m_aBackup[IO_MAX_PATH_LENGTH]{};
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_NO_FATAL_FAILURE(WriteStorageFile(m_pStorage.get(), TARGET, "old"));
			ASSERT_NO_FATAL_FAILURE(WriteStorageFile(m_pStorage.get(), CANDIDATE, "new"));
			m_pFaults = std::make_unique<CReplacementStorage>(*m_pStorage);
		}
		bool Replace() { return IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, TARGET, m_aBackup, sizeof(m_aBackup)); }
		void ExpectOldAndCandidate()
		{
			EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "old");
			EXPECT_EQ(ReadStorageFile(m_pStorage.get(), CANDIDATE), "new");
			EXPECT_FALSE(m_pStorage->FileExists(BACKUP, IStorage::TYPE_SAVE));
			EXPECT_FALSE(m_pFaults->m_UsedDestinationRemoval);
		}
		void ExpectRecoverableBackup()
		{
			EXPECT_FALSE(m_pStorage->FileExists(TARGET, IStorage::TYPE_SAVE));
			EXPECT_EQ(ReadStorageFile(m_pStorage.get(), BACKUP), "old");
			EXPECT_EQ(ReadStorageFile(m_pStorage.get(), CANDIDATE), "new");
			EXPECT_STREQ(m_aBackup, BACKUP);
			EXPECT_FALSE(m_pFaults->m_UsedDestinationRemoval);
		}
	};

	class StorageReplacePromotionException : public StorageReplaceTransaction, public ::testing::WithParamInterface<ERenameResult>
	{
	};
	class StorageReplaceRollbackFailure : public StorageReplaceTransaction, public ::testing::WithParamInterface<ERenameResult>
	{
	};
	class StorageReplaceCleanupFailure : public StorageReplaceTransaction, public ::testing::WithParamInterface<bool>
	{
	};
}

TEST_F(StorageReplaceTransaction, InitialLookupExceptionDoesNotMoveFiles)
{
	m_pFaults->m_ThrowInitialLookup = true;
	EXPECT_FALSE(Replace());
	ExpectOldAndCandidate();
	EXPECT_EQ(m_pFaults->m_Renames, 0);
}

TEST_F(StorageReplaceTransaction, BackupLookupExceptionDoesNotMoveFiles)
{
	m_pFaults->m_ThrowBackupLookup = true;
	EXPECT_FALSE(Replace());
	ExpectOldAndCandidate();
	EXPECT_EQ(m_pFaults->m_Renames, 0);
}

TEST_F(StorageReplaceTransaction, FailedBackupMoveKeepsOriginalInPlace)
{
	m_pFaults->m_StageResult = ERenameResult::REJECT;
	EXPECT_FALSE(Replace());
	ExpectOldAndCandidate();
}

TEST_F(StorageReplaceTransaction, ThrowingBackupMoveKeepsOriginalInPlace)
{
	m_pFaults->m_StageResult = ERenameResult::BAD_ALLOC;
	EXPECT_FALSE(Replace());
	ExpectOldAndCandidate();
}

TEST_F(StorageReplaceTransaction, ExceptionAfterBackupMoveStillAttemptsRestoration)
{
	m_pFaults->m_StageResult = ERenameResult::MOVE_THEN_THROW;
	EXPECT_FALSE(Replace());
	ExpectOldAndCandidate();
}

TEST_P(StorageReplacePromotionException, PromotionExceptionRestoresOldAndAllowsRetry)
{
	m_pFaults->m_PromotionResult = GetParam();
	EXPECT_FALSE(Replace());
	ExpectOldAndCandidate();
	m_pFaults->m_PromotionResult = ERenameResult::NATIVE;
	ASSERT_TRUE(Replace());
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "new");
	EXPECT_FALSE(m_pStorage->FileExists(CANDIDATE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(BACKUP, IStorage::TYPE_SAVE));
}

INSTANTIATE_TEST_SUITE_P(ExceptionKinds, StorageReplacePromotionException, ::testing::Values(ERenameResult::BAD_ALLOC, ERenameResult::OTHER_EXCEPTION));

TEST_P(StorageReplaceRollbackFailure, RollbackFailureRetainsOldBackupAndNewCandidate)
{
	m_pFaults->m_PromotionResult = ERenameResult::REJECT;
	m_pFaults->m_RollbackResult = GetParam();
	EXPECT_FALSE(Replace());
	ExpectRecoverableBackup();
}

INSTANTIATE_TEST_SUITE_P(FailureKinds, StorageReplaceRollbackFailure, ::testing::Values(ERenameResult::REJECT, ERenameResult::BAD_ALLOC));

TEST_F(StorageReplaceTransaction, FailedRollbackLookupRetainsRecoveryFiles)
{
	m_pFaults->m_PromotionResult = ERenameResult::REJECT;
	m_pFaults->m_ThrowRollbackLookup = true;
	EXPECT_FALSE(Replace());
	ExpectRecoverableBackup();
}

TEST_F(StorageReplaceTransaction, RollbackKeepsTargetPresentAtRecoveryCheck)
{
	m_pFaults->m_PromotionResult = ERenameResult::REJECT;
	m_pFaults->m_ConcurrentTarget = true;
	EXPECT_FALSE(Replace());
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "concurrent");
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), BACKUP), "old");
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), CANDIDATE), "new");
	EXPECT_FALSE(m_pFaults->m_UsedDestinationRemoval);
}

TEST_P(StorageReplaceCleanupFailure, CleanupFailureDoesNotChangeSuccessfulCommit)
{
	m_pFaults->m_ThrowCleanup = GetParam();
	m_pFaults->m_FailCleanup = !GetParam();
	ASSERT_TRUE(Replace());
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "new");
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), BACKUP), "old");
	EXPECT_FALSE(m_pStorage->FileExists(CANDIDATE, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pFaults->m_UsedDestinationRemoval);
}

INSTANTIATE_TEST_SUITE_P(FailureKinds, StorageReplaceCleanupFailure, ::testing::Bool());

TEST_F(StorageReplaceTransaction, ExistingBackupDirectoryDoesNotDamageAnyPath)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(BACKUP, IStorage::TYPE_SAVE));
	EXPECT_FALSE(Replace());
	EXPECT_TRUE(m_pStorage->FolderExists(BACKUP, IStorage::TYPE_SAVE));
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "old");
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), CANDIDATE), "new");
	EXPECT_FALSE(m_pFaults->m_UsedDestinationRemoval);
}

TEST_F(StorageReplaceTransaction, InsufficientBackupBufferCannotMoveOldFileToTruncatedPath)
{
	char aBackup[15]{};
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, TARGET, aBackup, sizeof(aBackup)));
	EXPECT_STREQ(aBackup, "");
	ExpectOldAndCandidate();
	EXPECT_EQ(m_pFaults->m_Renames, 0);
}

TEST_F(StorageReplaceTransaction, ExactBackupCapacityCanCommit)
{
	char aBackup[16]{};
	ASSERT_TRUE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, TARGET, aBackup, sizeof(aBackup)));
	EXPECT_STREQ(aBackup, BACKUP);
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "new");
	EXPECT_FALSE(m_pStorage->FileExists(BACKUP, IStorage::TYPE_SAVE));
}

TEST_F(StorageReplaceTransaction, NoPreviousFileNeedsOnlyEmptyBackupOutput)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(TARGET, IStorage::TYPE_SAVE));
	char aBackup[1]{'x'};
	ASSERT_TRUE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, TARGET, aBackup, sizeof(aBackup)));
	EXPECT_STREQ(aBackup, "");
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), TARGET), "new");
	EXPECT_FALSE(m_pStorage->FileExists(BACKUP, IStorage::TYPE_SAVE));
}

TEST_F(StorageReplaceTransaction, FailureWithoutPreviousFileDoesNotInventBackup)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(TARGET, IStorage::TYPE_SAVE));
	m_pFaults->m_PromotionResult = ERenameResult::BAD_ALLOC;
	EXPECT_FALSE(Replace());
	EXPECT_STREQ(m_aBackup, "");
	EXPECT_FALSE(m_pStorage->FileExists(TARGET, IStorage::TYPE_SAVE));
	EXPECT_FALSE(m_pStorage->FileExists(BACKUP, IStorage::TYPE_SAVE));
	EXPECT_EQ(ReadStorageFile(m_pStorage.get(), CANDIDATE), "new");
}

TEST_F(StorageReplaceTransaction, InvalidArgumentsLeaveOriginalAndCandidateUntouched)
{
	EXPECT_FALSE(IStorage::ReplaceFileSafely(nullptr, CANDIDATE, TARGET, m_aBackup, sizeof(m_aBackup)));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), nullptr, TARGET, m_aBackup, sizeof(m_aBackup)));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, nullptr, m_aBackup, sizeof(m_aBackup)));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, TARGET, nullptr, sizeof(m_aBackup)));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, TARGET, m_aBackup, 0));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), "", TARGET, m_aBackup, sizeof(m_aBackup)));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), CANDIDATE, "", m_aBackup, sizeof(m_aBackup)));
	EXPECT_FALSE(IStorage::ReplaceFileSafely(m_pFaults.get(), TARGET, TARGET, m_aBackup, sizeof(m_aBackup)));
	ExpectOldAndCandidate();
	EXPECT_EQ(m_pFaults->m_Renames, 0);
}
