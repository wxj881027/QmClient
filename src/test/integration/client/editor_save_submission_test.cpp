#include <game/editor/mapitems/save_job.h>
#include <game/editor/mapitems/save_submission.h>

#include <gtest/gtest.h>
#include <test/support/datafile_writer_operations.h>
#include <test/test.h>

#include <deque>
#include <memory>
#include <new>
#include <utility>

namespace
{
	class CSubmittedSaveJob : public CDataFileWriterFinishJob
	{
	public:
		using CDataFileWriterFinishJob::CDataFileWriterFinishJob;
		void RunNow() { Run(); }
	};

	class CTrackedMapSaves : public std::deque<std::shared_ptr<CDataFileWriterFinishJob>>
	{
	public:
		bool m_AllocationFails = false;
		void push_back(const std::shared_ptr<CDataFileWriterFinishJob> &pJob)
		{
			if(m_AllocationFails)
				throw std::bad_alloc();
			std::deque<std::shared_ptr<CDataFileWriterFinishJob>>::push_back(pJob);
		}
	};

	class EditorSaveSubmission : public ::testing::Test
	{
	protected:
		using EOutcome = EditorMapSave::EOutcome;
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;
		CTestDatafileWriterOperations m_Operations;
		CTrackedMapSaves m_Jobs;
		std::shared_ptr<CSubmittedSaveJob> m_pAccepted;
		std::weak_ptr<CSubmittedSaveJob> m_pPrepared;
		CJobPool *m_pClosedPool = nullptr;
		bool m_RejectPreparation = false;
		bool m_ThrowBeforeOpen = false;
		bool m_ThrowAfterOpen = false;
		bool m_ThrowAfterJob = false;
		bool m_RejectEnqueue = false;
		bool m_ThrowEnqueue = false;
		bool m_RunInline = false;
		bool m_ThrowReport = false;
		int m_Preparations = 0;
		int m_SpecificErrors = 0;
		int m_Enqueues = 0;
		int m_Reports = 0;
		size_t m_TrackedAtReport = 0;
		EOutcome m_ReportedOutcome = EOutcome::SUBMITTED;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			IOHANDLE File = m_pStorage->OpenFile("map.map", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			ASSERT_NE(File, nullptr);
			EXPECT_EQ(io_write(File, "previous", 8), 8);
			EXPECT_EQ(io_close(File), 0);
		}
		std::shared_ptr<CSubmittedSaveJob> Prepare()
		{
			++m_Preparations;
			if(m_RejectPreparation)
			{
				++m_SpecificErrors;
				return nullptr;
			}
			if(m_ThrowBeforeOpen)
				throw std::bad_alloc();
			CDataFileWriter Writer(&m_Operations);
			if(!Writer.Open(m_pStorage.get(), "candidate.map"))
			{
				ADD_FAILURE() << "could not open test candidate";
				return nullptr;
			}
			EXPECT_EQ(Writer.AddDataString("next"), 0);
			if(m_ThrowAfterOpen)
				throw std::bad_alloc();
			auto pJob = std::make_shared<CSubmittedSaveJob>(m_pStorage.get(), nullptr, "map.map", "candidate.map", std::move(Writer));
			m_pPrepared = pJob;
			if(m_ThrowAfterJob)
				throw std::bad_alloc();
			return pJob;
		}
		EOutcome Submit()
		{
			return EditorMapSave::Submit(m_Jobs, [&] { return Prepare(); }, [&](const std::shared_ptr<CSubmittedSaveJob> &pJob) {
					++m_Enqueues;
					EXPECT_FALSE(m_Jobs.empty());
					EXPECT_EQ(m_Jobs.back(), pJob);
					EXPECT_EQ(m_Operations.m_Closes, 0);
					if(m_ThrowEnqueue)
						throw std::bad_alloc();
					if(m_RejectEnqueue)
						return false;
					if(m_pClosedPool)
						return m_pClosedPool->TryAdd(pJob);
					if(m_RunInline)
						pJob->RunNow();
					m_pAccepted = pJob;
					return true; }, [&](EOutcome Outcome) {
					++m_Reports;
					m_ReportedOutcome = Outcome;
					m_TrackedAtReport = m_Jobs.size();
					EXPECT_TRUE(m_pPrepared.expired());
					if(m_ThrowReport)
						throw std::bad_alloc(); });
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
		void ExpectNewMap()
		{
			CDataFileReader Reader;
			ASSERT_TRUE(Reader.Open(m_pStorage.get(), "map.map", IStorage::TYPE_SAVE));
			EXPECT_STREQ(Reader.GetDataString(0), "next");
		}
	};
}

TEST_F(EditorSaveSubmission, AcceptedSaveRemainsTrackedUntilItsResultCanBeConsumed)
{
	ASSERT_EQ(Submit(), EOutcome::SUBMITTED);
	ASSERT_EQ(m_Jobs.size(), 1U);
	ASSERT_NE(m_pAccepted, nullptr);
	EXPECT_EQ(m_Jobs.back(), m_pAccepted);
	EXPECT_EQ(m_Reports, 0);
	ExpectPrevious();
	m_pAccepted->RunNow();
	EXPECT_STREQ(m_pAccepted->ErrorMessage(), "");
	ExpectNewMap();
}

TEST_F(EditorSaveSubmission, SpecificPreparationFailureDoesNotReportTwiceOrEnqueue)
{
	m_RejectPreparation = true;
	EXPECT_EQ(Submit(), EOutcome::PREPARATION_REJECTED);
	EXPECT_EQ(m_SpecificErrors, 1);
	EXPECT_EQ(m_Reports, 0);
	EXPECT_EQ(m_Enqueues, 0);
	EXPECT_TRUE(m_Jobs.empty());
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, PreparationExceptionBeforeOpenReturnsFailure)
{
	m_ThrowBeforeOpen = true;
	EXPECT_EQ(Submit(), EOutcome::PREPARATION_FAILED);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_ReportedOutcome, EOutcome::PREPARATION_FAILED);
	EXPECT_EQ(m_Enqueues, 0);
	EXPECT_EQ(m_Operations.m_Closes, 0);
	EXPECT_TRUE(m_Jobs.empty());
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, ExceptionAfterOpeningCandidateClosesBeforeReportingFailure)
{
	m_ThrowAfterOpen = true;
	EXPECT_EQ(Submit(), EOutcome::PREPARATION_FAILED);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_TrackedAtReport, 0U);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Writes, 0);
	EXPECT_EQ(m_Enqueues, 0);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, ExceptionAfterCreatingJobReleasesMovedWriter)
{
	m_ThrowAfterJob = true;
	EXPECT_EQ(Submit(), EOutcome::PREPARATION_FAILED);
	EXPECT_TRUE(m_pPrepared.expired());
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Enqueues, 0);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, TrackingAllocationFailureDoesNotStartBackgroundSave)
{
	m_Jobs.m_AllocationFails = true;
	EXPECT_EQ(Submit(), EOutcome::TRACKING_FAILED);
	EXPECT_EQ(m_ReportedOutcome, EOutcome::TRACKING_FAILED);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_Enqueues, 0);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_TRUE(m_pPrepared.expired());
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Operations.m_Writes, 0);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, EnqueueRejectionRemovesOnlyTheUnacceptedSave)
{
	m_RejectEnqueue = true;
	EXPECT_EQ(Submit(), EOutcome::QUEUE_FAILED);
	EXPECT_EQ(m_ReportedOutcome, EOutcome::QUEUE_FAILED);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_TrackedAtReport, 0U);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, EnqueueExceptionRollsBackTrackingAndClosesCandidate)
{
	m_ThrowEnqueue = true;
	EXPECT_EQ(Submit(), EOutcome::QUEUE_FAILED);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_TRUE(m_pPrepared.expired());
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, ClosedRealJobPoolCannotLeaveAnUnfinishableTrackedSave)
{
	CJobPool Pool;
	m_pClosedPool = &Pool;
	EXPECT_EQ(Submit(), EOutcome::QUEUE_FAILED);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Reports, 1);
	EXPECT_TRUE(m_pPrepared.expired());
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, RejectedSavePreservesPreviouslyTrackedJobs)
{
	CDataFileWriter PreviousWriter;
	auto pPrevious = std::make_shared<CSubmittedSaveJob>(m_pStorage.get(), nullptr, "older.map", "older.tmp", std::move(PreviousWriter));
	m_Jobs.push_back(pPrevious);
	m_RejectEnqueue = true;
	EXPECT_EQ(Submit(), EOutcome::QUEUE_FAILED);
	ASSERT_EQ(m_Jobs.size(), 1U);
	EXPECT_EQ(m_Jobs.front(), pPrevious);
	EXPECT_EQ(m_TrackedAtReport, 1U);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, InlineCompletionCanObserveTrackingAndPublishTheCandidate)
{
	m_RunInline = true;
	ASSERT_EQ(Submit(), EOutcome::SUBMITTED);
	ASSERT_EQ(m_Jobs.size(), 1U);
	ASSERT_NE(m_pAccepted, nullptr);
	EXPECT_EQ(m_Jobs.back(), m_pAccepted);
	EXPECT_STREQ(m_pAccepted->ErrorMessage(), "");
	EXPECT_EQ(m_Operations.m_Closes, 1);
	EXPECT_EQ(m_Reports, 0);
	ExpectNewMap();
}

TEST_F(EditorSaveSubmission, ErrorNotificationExceptionDoesNotEscapeOrKeepTracking)
{
	m_RejectEnqueue = true;
	m_ThrowReport = true;
	EXPECT_EQ(Submit(), EOutcome::QUEUE_FAILED);
	EXPECT_EQ(m_Reports, 1);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectPrevious();
}

TEST_F(EditorSaveSubmission, TrackingFailureCanRetryWithANewCandidate)
{
	m_Jobs.m_AllocationFails = true;
	ASSERT_EQ(Submit(), EOutcome::TRACKING_FAILED);
	ExpectPrevious();
	m_Jobs.m_AllocationFails = false;
	m_Operations.m_Closes = 0;
	ASSERT_EQ(Submit(), EOutcome::SUBMITTED);
	ASSERT_NE(m_pAccepted, nullptr);
	m_pAccepted->RunNow();
	EXPECT_EQ(m_Reports, 1);
	EXPECT_EQ(m_Operations.m_Closes, 1);
	ExpectNewMap();
}

TEST_F(EditorSaveSubmission, EnqueueFailureCanRetryWithoutStaleBusyEntry)
{
	m_RejectEnqueue = true;
	ASSERT_EQ(Submit(), EOutcome::QUEUE_FAILED);
	ASSERT_TRUE(m_Jobs.empty());
	m_RejectEnqueue = false;
	m_Operations.m_Closes = 0;
	ASSERT_EQ(Submit(), EOutcome::SUBMITTED);
	ASSERT_EQ(m_Jobs.size(), 1U);
	ASSERT_NE(m_pAccepted, nullptr);
	m_pAccepted->RunNow();
	EXPECT_STREQ(m_pAccepted->ErrorMessage(), "");
	ExpectNewMap();
}

TEST_F(EditorSaveSubmission, TrackedReferenceKeepsAcceptedSaveAliveAfterSubmitReturns)
{
	ASSERT_EQ(Submit(), EOutcome::SUBMITTED);
	m_pAccepted.reset();
	auto pJob = m_pPrepared.lock();
	ASSERT_NE(pJob, nullptr);
	ASSERT_EQ(m_Jobs.size(), 1U);
	pJob->RunNow();
	ExpectNewMap();
	pJob.reset();
	m_Jobs.clear();
	EXPECT_TRUE(m_pPrepared.expired());
	EXPECT_EQ(m_Operations.m_Closes, 1);
}
