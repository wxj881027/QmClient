#include <game/client/components/qmclient/map_history_storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
	class CFileFaults : public qm_map_history_storage::CFileOperations
	{
	public:
		enum class EFailure
		{
			NONE,
			SERIALIZE_ALLOCATION,
			SERIALIZE_LENGTH,
			OPEN,
			OPEN_CREATED_FAILURE,
			OPEN_CREATED_THROW,
			WRITE,
			WRITE_THROW,
			SYNC,
			ERROR_FLAG,
			CLOSE,
			PUBLISH,
			PUBLISH_THROW,
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Opens = 0, m_Closes = 0, m_Publishes = 0;
		bool m_PublishedAfterClose = false;
		bool m_CreatedBeforeOpenFailure = false;
		std::string Serialize(const QmMapHistory::CMapHistory &History) override
		{
			if(m_Failure == EFailure::SERIALIZE_ALLOCATION)
				throw std::bad_alloc();
			if(m_Failure == EFailure::SERIALIZE_LENGTH)
				throw std::length_error("history serialization");
			return CFileOperations::Serialize(History);
		}
		IOHANDLE Open(IStorage &Storage, const char *pPath) override
		{
			++m_Opens;
			if(m_Failure == EFailure::OPEN)
				return nullptr;
			IOHANDLE File = CFileOperations::Open(Storage, pPath);
			if(m_Failure == EFailure::OPEN_CREATED_THROW || m_Failure == EFailure::OPEN_CREATED_FAILURE)
			{
				// 模拟创建文件后、交付句柄前失败；打开方释放其内部资源。
				m_CreatedBeforeOpenFailure = File != nullptr;
				if(File)
					io_close(File);
				if(m_Failure == EFailure::OPEN_CREATED_FAILURE)
					return nullptr;
				throw std::bad_alloc();
			}
			return File;
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			if(m_Failure == EFailure::WRITE_THROW)
			{
				CFileOperations::Write(File, pData, Size / 2);
				throw std::bad_alloc();
			}
			return CFileOperations::Write(File, pData, m_Failure == EFailure::WRITE ? Size - 1 : Size);
		}
		int Sync(IOHANDLE File) override
		{
			const int Result = CFileOperations::Sync(File);
			return m_Failure == EFailure::SYNC ? -1 : Result;
		}
		int Error(IOHANDLE File) override
		{
			const int Result = CFileOperations::Error(File);
			return m_Failure == EFailure::ERROR_FLAG ? 1 : Result;
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_Closes;
			const int Result = CFileOperations::Close(File);
			return m_Failure == EFailure::CLOSE ? -1 : Result;
		}
		bool Publish(IStorage &Storage, const char *pTemporary) override
		{
			++m_Publishes;
			m_PublishedAfterClose = m_Closes == m_Opens;
			if(m_Failure == EFailure::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Failure != EFailure::PUBLISH && CFileOperations::Publish(Storage, pTemporary);
		}
	};

	class CQmMapHistoryStorageTest : public ::testing::Test
	{
	protected:
		using EFailure = CFileFaults::EFailure;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CFileFaults m_Operations;
		QmMapHistory::CMapHistory m_History;
		bool m_Dirty = true;
		const std::string m_Old = R"({"version":1,"entries":[]})";

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_TRUE(m_pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE));
			Store(qm_map_history_storage::FILE_PATH, m_Old);
			m_History.RecordVisit("New map with \"quotes\" and 新地图", "path:maps/new.map", 123, "2026-11-01");
			m_History.AddDeath("path:maps/new.map", 3);
			m_History.MarkFinished("path:maps/new.map", 500, 750);
		}
		void Store(const char *pPath, const std::string &Content)
		{
			IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, Content.data(), Content.size()), Content.size());
			EXPECT_EQ(io_close(File), 0);
		}
		std::string Read(const char *pPath)
		{
			void *pRaw = nullptr;
			unsigned Size = 0;
			EXPECT_TRUE(m_pStorage->ReadFile(pPath, IStorage::TYPE_SAVE, &pRaw, &Size));
			const std::unique_ptr<void, decltype(&free)> Data(pRaw, free);
			return pRaw ? std::string(static_cast<const char *>(pRaw), Size) : "";
		}
		std::string Temporary()
		{
			char aPath[IO_MAX_PATH_LENGTH];
			IStorage::FormatTmpPath(aPath, sizeof(aPath), qm_map_history_storage::FILE_PATH);
			return aPath;
		}
		bool Save()
		{
			return qm_map_history_storage::Save(m_pStorage.get(), m_History, m_Dirty, &m_Operations);
		}
		void ExpectOldFile(bool CandidateAbsent = true)
		{
			EXPECT_TRUE(m_Dirty);
			EXPECT_EQ(Read(qm_map_history_storage::FILE_PATH), m_Old);
			if(CandidateAbsent)
				EXPECT_FALSE(m_pStorage->FileExists(Temporary().c_str(), IStorage::TYPE_SAVE));
		}
		void FailedSave(EFailure Failure, int Opens = 1, int Closes = 1, int Publishes = 0)
		{
			m_Operations.m_Failure = Failure;
			const std::string Before = m_History.ToJson();
			EXPECT_FALSE(Save());
			ExpectOldFile();
			EXPECT_EQ(m_History.ToJson(), Before);
			EXPECT_EQ(m_Operations.m_Opens, Opens);
			EXPECT_EQ(m_Operations.m_Closes, Closes);
			EXPECT_EQ(m_Operations.m_Publishes, Publishes);
		}
	};
}

TEST_F(CQmMapHistoryStorageTest, SuccessfulSaveClosesBeforeReplacementAndClearsDirty)
{
	ASSERT_TRUE(Save());
	EXPECT_FALSE(m_Dirty);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Publishes, 1);
	EXPECT_TRUE(m_Operations.m_PublishedAfterClose);
	EXPECT_FALSE(m_pStorage->FileExists(Temporary().c_str(), IStorage::TYPE_SAVE));
	const std::string Json = Read(qm_map_history_storage::FILE_PATH);
	EXPECT_EQ(Json, m_History.ToJson());
	QmMapHistory::CMapHistory Loaded;
	ASSERT_TRUE(Loaded.FromJson(Json));
	EXPECT_EQ(Loaded.ToJson(), Json);
}

TEST_F(CQmMapHistoryStorageTest, FirstSaveCreatesTheTargetWithoutAnOldFile)
{
	ASSERT_TRUE(m_pStorage->RemoveFile(qm_map_history_storage::FILE_PATH, IStorage::TYPE_SAVE));
	ASSERT_TRUE(Save());
	EXPECT_FALSE(m_Dirty);
	EXPECT_EQ(Read(qm_map_history_storage::FILE_PATH), m_History.ToJson());
}

TEST_F(CQmMapHistoryStorageTest, SerializationAllocationFailureDoesNotOpenAFile)
{
	FailedSave(EFailure::SERIALIZE_ALLOCATION, 0, 0);
}

TEST_F(CQmMapHistoryStorageTest, SerializationLengthFailureDoesNotOpenAFile)
{
	FailedSave(EFailure::SERIALIZE_LENGTH, 0, 0);
}

TEST_F(CQmMapHistoryStorageTest, OpenFailureRetainsTheSavedHistoryAndDirtyState)
{
	FailedSave(EFailure::OPEN, 1, 0);
}

TEST_F(CQmMapHistoryStorageTest, OpenExceptionCleansACandidateCreatedBeforeHandleDelivery)
{
	FailedSave(EFailure::OPEN_CREATED_THROW, 1, 0);
	EXPECT_TRUE(m_Operations.m_CreatedBeforeOpenFailure);
}

TEST_F(CQmMapHistoryStorageTest, OpenFailureCleansACandidateCreatedBeforeHandleDelivery)
{
	FailedSave(EFailure::OPEN_CREATED_FAILURE, 1, 0);
	EXPECT_TRUE(m_Operations.m_CreatedBeforeOpenFailure);
}

TEST_F(CQmMapHistoryStorageTest, ShortWriteClosesAndDiscardsThePartialCandidate)
{
	FailedSave(EFailure::WRITE);
}

TEST_F(CQmMapHistoryStorageTest, WriteExceptionClosesAndDiscardsThePartialCandidate)
{
	FailedSave(EFailure::WRITE_THROW);
}

TEST_F(CQmMapHistoryStorageTest, SyncFailureRetainsTheSavedHistoryAndDirtyState)
{
	FailedSave(EFailure::SYNC);
}

TEST_F(CQmMapHistoryStorageTest, ErrorFlagPreventsReplacementAfterAllBytesWereWritten)
{
	FailedSave(EFailure::ERROR_FLAG);
}

TEST_F(CQmMapHistoryStorageTest, CloseFailurePreventsReplacementWithoutClosingTwice)
{
	FailedSave(EFailure::CLOSE);
}

TEST_F(CQmMapHistoryStorageTest, ReplacementFailureRetainsTheSavedHistoryAndDirtyState)
{
	FailedSave(EFailure::PUBLISH, 1, 1, 1);
	EXPECT_TRUE(m_Operations.m_PublishedAfterClose);
}

TEST_F(CQmMapHistoryStorageTest, ReplacementExceptionRetainsTheSavedHistoryAndDirtyState)
{
	FailedSave(EFailure::PUBLISH_THROW, 1, 1, 1);
	EXPECT_TRUE(m_Operations.m_PublishedAfterClose);
}

TEST_F(CQmMapHistoryStorageTest, ExistingCandidateFileIsPreservedWithoutOpeningIt)
{
	Store(Temporary().c_str(), "prior candidate");
	EXPECT_FALSE(Save());
	ExpectOldFile(false);
	EXPECT_EQ(Read(Temporary().c_str()), "prior candidate");
	EXPECT_EQ(m_Operations.m_Opens, 0);
}

TEST_F(CQmMapHistoryStorageTest, ExistingCandidateDirectoryIsPreservedWithoutOpeningIt)
{
	ASSERT_TRUE(m_pStorage->CreateFolder(Temporary().c_str(), IStorage::TYPE_SAVE));
	EXPECT_FALSE(Save());
	ExpectOldFile();
	EXPECT_TRUE(m_pStorage->FolderExists(Temporary().c_str(), IStorage::TYPE_SAVE));
	EXPECT_EQ(m_Operations.m_Opens, 0);
}

TEST_F(CQmMapHistoryStorageTest, ExistingBackupIsUntouchedBySuccessfulReplacement)
{
	const std::string Backup = Temporary() + ".backup";
	Store(Backup.c_str(), "prior backup");
	ASSERT_TRUE(Save());
	EXPECT_EQ(Read(Backup.c_str()), "prior backup");
	EXPECT_EQ(Read(qm_map_history_storage::FILE_PATH), m_History.ToJson());
}

TEST_F(CQmMapHistoryStorageTest, RetryAfterCloseFailureCanSaveTheCurrentHistory)
{
	FailedSave(EFailure::CLOSE);
	m_History.RecordVisit("Later map", "path:maps/later.map", 456, "2026-11-01");
	m_Operations.m_Failure = EFailure::NONE;
	ASSERT_TRUE(Save());
	EXPECT_FALSE(m_Dirty);
	EXPECT_EQ(Read(qm_map_history_storage::FILE_PATH), m_History.ToJson());
	EXPECT_EQ(m_Operations.m_Closes, 2);
	EXPECT_EQ(m_Operations.m_Publishes, 1);
}

TEST_F(CQmMapHistoryStorageTest, FailedExplicitSaveDoesNotInventChangesForACleanModel)
{
	m_Dirty = false;
	m_Operations.m_Failure = EFailure::CLOSE;
	EXPECT_FALSE(Save());
	EXPECT_FALSE(m_Dirty);
	EXPECT_EQ(Read(qm_map_history_storage::FILE_PATH), m_Old);
}

TEST_F(CQmMapHistoryStorageTest, MissingStoragePreservesThePendingState)
{
	EXPECT_FALSE(qm_map_history_storage::Save(nullptr, m_History, m_Dirty));
	EXPECT_TRUE(m_Dirty);
}
