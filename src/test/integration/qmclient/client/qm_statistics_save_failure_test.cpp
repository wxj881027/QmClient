#include <game/client/components/qmclient/statistics_save_job.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <string>

namespace
{
	constexpr char FILE_PATH[] = "qmclient/statistics.json";
	constexpr char OLD[] = "{\"local\":{},\"remote\":{}}";
	constexpr char NEW[] = "{\"local\":{\"modes\":[]},\"remote\":{}}";
	enum class EFault
	{
		NONE,
		CREATE_THROW,
		OPEN_THROW,
		OPEN_NULL,
		WRITE_THROW,
		WRITE_SHORT,
		SYNC_THROW,
		SYNC_ERROR,
		ERROR_THROW,
		ERROR_FLAG,
		CLOSE_ERROR,
		PUBLISH_THROW,
		PUBLISH_FALSE,
	};

	class COperations final : public CQmStatisticsSaveJob::CFileOperations
	{
	public:
		EFault m_Fault = EFault::NONE;
		bool m_RemoveThrows = false;
		IOHANDLE m_File = nullptr;
		int m_OpenCalls = 0;
		int m_CloseCalls = 0;
		int m_RemoveCalls = 0;
		int m_PublishCalls = 0;
		void CreateFolder(IStorage &Storage) override
		{
			if(m_Fault == EFault::CREATE_THROW)
				throw std::bad_alloc();
			CFileOperations::CreateFolder(Storage);
		}
		IOHANDLE Open(IStorage &Storage, const char *pPath) override
		{
			++m_OpenCalls;
			if(m_Fault == EFault::OPEN_THROW)
				throw std::bad_alloc();
			if(m_Fault == EFault::OPEN_NULL)
				return nullptr;
			m_File = CFileOperations::Open(Storage, pPath);
			return m_File;
		}
		unsigned Write(IOHANDLE File, const void *pData, unsigned Size) override
		{
			if(m_Fault == EFault::WRITE_THROW)
				throw std::bad_alloc();
			return CFileOperations::Write(File, pData, m_Fault == EFault::WRITE_SHORT ? Size - 1 : Size);
		}
		int Sync(IOHANDLE File) override
		{
			if(m_Fault == EFault::SYNC_THROW)
				throw std::bad_alloc();
			const int Result = CFileOperations::Sync(File);
			return m_Fault == EFault::SYNC_ERROR ? -1 : Result;
		}
		int Error(IOHANDLE File) override
		{
			if(m_Fault == EFault::ERROR_THROW)
				throw std::bad_alloc();
			return m_Fault == EFault::ERROR_FLAG ? 1 : CFileOperations::Error(File);
		}
		int Close(IOHANDLE File) noexcept override
		{
			++m_CloseCalls;
			m_File = nullptr;
			const int Result = CFileOperations::Close(File);
			return m_Fault == EFault::CLOSE_ERROR ? -1 : Result;
		}
		bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget) override
		{
			++m_PublishCalls;
			if(m_Fault == EFault::PUBLISH_THROW)
				throw std::bad_alloc();
			return m_Fault != EFault::PUBLISH_FALSE && CFileOperations::Publish(Storage, pTemporary, pTarget);
		}
		void Remove(IStorage &Storage, const char *pPath) override
		{
			++m_RemoveCalls;
			if(m_RemoveThrows)
				throw std::bad_alloc();
			CFileOperations::Remove(Storage, pPath);
		}
	};

	class QmStatisticsSaveFailure : public ::testing::TestWithParam<EFault>
	{
		CTestInfo m_Info;

	protected:
		std::unique_ptr<IStorage> m_pStorage;
		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			CQmStatisticsSaveJob Initial(m_pStorage.get(), FILE_PATH, OLD);
			ASSERT_TRUE(Initial.Finish());
		}
		std::string Read()
		{
			std::unique_ptr<char, decltype(&free)> Data(m_pStorage->ReadFileStr(FILE_PATH, IStorage::TYPE_SAVE), free);
			return Data ? Data.get() : "";
		}
	};
}

TEST_P(QmStatisticsSaveFailure, FailedWriteKeepsOldFileClosesHandleAndFreshJobRecovers)
{
	auto pOperations = std::make_shared<COperations>();
	pOperations->m_Fault = GetParam();
	CQmStatisticsSaveJob Job(m_pStorage.get(), FILE_PATH, NEW, pOperations);
	EXPECT_FALSE(Job.Finish());
	EXPECT_EQ(Read(), OLD);
	EXPECT_EQ(pOperations->m_File, nullptr);
	EXPECT_EQ(pOperations->m_CloseCalls, GetParam() == EFault::CREATE_THROW || GetParam() == EFault::OPEN_THROW || GetParam() == EFault::OPEN_NULL ? 0 : 1);
	const int Opens = pOperations->m_OpenCalls;
	const int Removes = pOperations->m_RemoveCalls;
	EXPECT_FALSE(Job.Finish());
	EXPECT_EQ(pOperations->m_OpenCalls, Opens);
	EXPECT_EQ(pOperations->m_RemoveCalls, Removes);
	CQmStatisticsSaveJob Retry(m_pStorage.get(), FILE_PATH, NEW);
	ASSERT_TRUE(Retry.Finish());
	EXPECT_EQ(Read(), NEW);
}

INSTANTIATE_TEST_SUITE_P(IoStages, QmStatisticsSaveFailure, ::testing::Values(EFault::CREATE_THROW, EFault::OPEN_THROW, EFault::OPEN_NULL, EFault::WRITE_THROW, EFault::WRITE_SHORT, EFault::SYNC_THROW, EFault::SYNC_ERROR, EFault::ERROR_THROW, EFault::ERROR_FLAG, EFault::CLOSE_ERROR, EFault::PUBLISH_THROW, EFault::PUBLISH_FALSE));

TEST_F(QmStatisticsSaveFailure, SuccessfulCommitClosesOnceAndDoesNotRemoveCandidateAgain)
{
	auto pOperations = std::make_shared<COperations>();
	CQmStatisticsSaveJob Job(m_pStorage.get(), FILE_PATH, NEW, pOperations);
	ASSERT_TRUE(Job.Finish());
	EXPECT_EQ(Read(), NEW);
	EXPECT_EQ(pOperations->m_OpenCalls, 1);
	EXPECT_EQ(pOperations->m_CloseCalls, 1);
	EXPECT_EQ(pOperations->m_PublishCalls, 1);
	EXPECT_EQ(pOperations->m_RemoveCalls, 0);
	EXPECT_TRUE(Job.Finish());
	EXPECT_EQ(pOperations->m_OpenCalls, 1);
}

TEST_F(QmStatisticsSaveFailure, CleanupExceptionDoesNotEscapeAndNextSnapshotCanCommit)
{
	auto pOperations = std::make_shared<COperations>();
	pOperations->m_Fault = EFault::PUBLISH_FALSE;
	pOperations->m_RemoveThrows = true;
	CQmStatisticsSaveJob Job(m_pStorage.get(), FILE_PATH, NEW, pOperations);
	EXPECT_FALSE(Job.Finish());
	EXPECT_EQ(Read(), OLD);
	EXPECT_EQ(pOperations->m_CloseCalls, 1);
	EXPECT_EQ(pOperations->m_RemoveCalls, 1);
	EXPECT_FALSE(Job.Finish());
	EXPECT_EQ(pOperations->m_RemoveCalls, 1);
	CQmStatisticsSaveJob Retry(m_pStorage.get(), FILE_PATH, NEW);
	ASSERT_TRUE(Retry.Finish());
	EXPECT_EQ(Read(), NEW);
}
