#include <game/editor/mapitems/save_job.h>

#include <gtest/gtest.h>
#include <test/support/datafile_writer_operations.h>
#include <test/test.h>

#include <memory>
#include <new>
#include <utility>

namespace
{
	class CSaveJobForTest : public CDataFileWriterFinishJob
	{
	public:
		using CDataFileWriterFinishJob::CDataFileWriterFinishJob;
		void RunNow() { Run(); }
	};

	class CSavePublicationOperations : public CDataFileWriterFinishJob::IOperations
	{
	public:
		bool m_Throw = true;
		bool m_ThrowAfterBackup = false;
		int m_Attempts = 0;
		bool Replace(IStorage &Storage, const char *pTemporary, const char *pDestination, char *pBackup, unsigned BackupSize) override
		{
			++m_Attempts;
			if(m_ThrowAfterBackup)
			{
				// 在外部发布边界安排“旧文件已备份、下一次文件操作失败”的状态。
				str_format(pBackup, BackupSize, "%s.backup", pTemporary);
				if(!Storage.RenameFile(pDestination, pBackup, IStorage::TYPE_SAVE))
					return false;
				throw std::bad_alloc();
			}
			if(m_Throw)
				throw std::bad_alloc();
			return IOperations::Replace(Storage, pTemporary, pDestination, pBackup, BackupSize);
		}
	};

	class EditorSaveJob : public ::testing::Test
	{
	protected:
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CTestDatafileWriterOperations m_Operations;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
		}
		void CreatePrevious()
		{
			IOHANDLE File = m_pStorage->OpenFile("map.map", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "previous", 8), 8);
			EXPECT_EQ(io_close(File), 0);
		}
		void Prepare(CDataFileWriter &Writer)
		{
			ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
			ASSERT_EQ(Writer.AddDataString("new map"), 0);
		}
		void ExpectPrevious()
		{
			IOHANDLE File = m_pStorage->OpenFile("map.map", IOFLAG_READ, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			char aBuffer[16]{};
			EXPECT_EQ(io_read(File, aBuffer, sizeof(aBuffer)), 8);
			EXPECT_STREQ(aBuffer, "previous");
			EXPECT_EQ(io_close(File), 0);
		}
	};
}

TEST_F(EditorSaveJob, CompleteCandidateReplacesPreviousFile)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_STREQ(Job.ErrorMessage(), "");
	EXPECT_FALSE(m_pStorage->FileExists("candidate.map", IStorage::TYPE_SAVE));
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "map.map", IStorage::TYPE_SAVE));
	EXPECT_STREQ(Reader.GetDataString(0), "new map");
}

TEST_F(EditorSaveJob, PreparationFailureKeepsPreviousFileAndReportsError)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	ASSERT_TRUE(Writer.Open(m_pStorage.get(), "candidate.map"));
	m_Operations.m_FailAllocationAt = 1;
	ASSERT_EQ(Writer.AddDataString("new map"), -1);
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	ExpectPrevious();
	EXPECT_EQ(m_Operations.m_Writes, 0);
}

TEST_F(EditorSaveJob, CompressionFailureKeepsPreviousFileAndReportsError)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_CompressionFails = true;
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	ExpectPrevious();
}

TEST_F(EditorSaveJob, ShortWriteKeepsPreviousFileAndReportsError)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_ShortWriteAt = 2;
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	ExpectPrevious();
}

TEST_F(EditorSaveJob, FailedCloseKeepsPreviousFileAndReportsError)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_CloseFails = true;
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	ExpectPrevious();
	EXPECT_EQ(m_Operations.m_Closes, 1);
}

TEST_F(EditorSaveJob, FirstSaveFailureDoesNotCreateDestination)
{
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	m_Operations.m_CloseFails = true;
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	EXPECT_FALSE(m_pStorage->FileExists("map.map", IStorage::TYPE_SAVE));
}

TEST_F(EditorSaveJob, PublicationFailureKeepsPreviousFileAndReportsError)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "missing-candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	ExpectPrevious();
}

TEST_F(EditorSaveJob, LaterSaveCanSucceedAfterFailedCandidate)
{
	CreatePrevious();
	{
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
		m_Operations.m_CloseFails = true;
		CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
		Job.RunNow();
		ASSERT_NE(Job.ErrorMessage()[0], '\0');
	}
	ExpectPrevious();
	m_Operations.m_CloseFails = false;
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
	Job.RunNow();
	EXPECT_STREQ(Job.ErrorMessage(), "");
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "map.map", IStorage::TYPE_SAVE));
	EXPECT_STREQ(Reader.GetDataString(0), "new map");
}

TEST_F(EditorSaveJob, PublicationExceptionReportsFailureAndKeepsPreviousFile)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	CSavePublicationOperations Operations;
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer), &Operations);
	EXPECT_NO_THROW(Job.RunNow());
	EXPECT_NE(Job.ErrorMessage()[0], '\0');
	EXPECT_EQ(Operations.m_Attempts, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectPrevious();
}

TEST_F(EditorSaveJob, NewSaveCanPublishAfterPublicationException)
{
	CreatePrevious();
	CSavePublicationOperations Operations;
	{
		CDataFileWriter Writer(&m_Operations);
		Prepare(Writer);
		CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer), &Operations);
		Job.RunNow();
		ASSERT_NE(Job.ErrorMessage()[0], '\0');
	}
	ExpectPrevious();
	Operations.m_Throw = false;
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer), &Operations);
	Job.RunNow();
	EXPECT_STREQ(Job.ErrorMessage(), "");
	EXPECT_EQ(Operations.m_Attempts, 2);
	CDataFileReader Reader;
	ASSERT_TRUE(Reader.Open(m_pStorage.get(), "map.map", IStorage::TYPE_SAVE));
	EXPECT_STREQ(Reader.GetDataString(0), "new map");
}

TEST_F(EditorSaveJob, PublicationExceptionAfterBackupReportsThePreservedFilePath)
{
	CreatePrevious();
	CDataFileWriter Writer(&m_Operations);
	Prepare(Writer);
	CSavePublicationOperations Operations;
	Operations.m_ThrowAfterBackup = true;
	CSaveJobForTest Job(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer), &Operations);
	EXPECT_NO_THROW(Job.RunNow());
	ASSERT_NE(Job.ErrorMessage()[0], '\0');
	EXPECT_NE(str_find(Job.ErrorMessage(), "candidate.map.backup"), nullptr);
	IOHANDLE File = m_pStorage->OpenFile("candidate.map.backup", IOFLAG_READ, IStorage::TYPE_SAVE);
	ASSERT_NE(File, nullptr);
	char aBuffer[16]{};
	EXPECT_EQ(io_read(File, aBuffer, sizeof(aBuffer)), 8);
	EXPECT_STREQ(aBuffer, "previous");
	EXPECT_EQ(io_close(File), 0);
}
