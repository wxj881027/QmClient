#include <engine/client/demoedit.h>
#include <engine/client/replay_save.h>

#include <gtest/gtest.h>
#include <test/support/demo_storage_state.h>

#include <deque>
#include <memory>
#include <new>

namespace
{
	class CReplayJob : public CDemoEdit
	{
	public:
		using CDemoEdit::CDemoEdit;
		void RunNow() { Run(); }
	};

	class CRecorderBoundary : public IDemoRecorder
	{
		CDemoRecorder &m_Recorder;

	public:
		enum class EFailure
		{
			NONE,
			RETURN_ERROR,
			WRITE_ERROR,
			THROW,
			STILL_RECORDING,
		};
		EFailure m_Failure = EFailure::NONE;
		int m_Stops = 0;
		explicit CRecorderBoundary(CDemoRecorder &Recorder) :
			m_Recorder(Recorder) {}
		bool IsRecording() const override { return m_Recorder.IsRecording(); }
		bool HasWriteError() const override { return m_Failure == EFailure::WRITE_ERROR || m_Recorder.HasWriteError(); }
		int Length() const override { return m_Recorder.Length(); }
		const char *CurrentFilename() const override { return m_Recorder.CurrentFilename(); }
		int Stop(EStopMode Mode, const char *pTarget = "") override
		{
			++m_Stops;
			if(m_Failure == EFailure::THROW)
				throw 7;
			if(m_Failure == EFailure::STILL_RECORDING)
				return 0;
			const int Result = m_Recorder.Stop(Mode, pTarget);
			return m_Failure == EFailure::RETURN_ERROR ? -1 : Result;
		}
	};

	class CTrackedReplayJobs : public std::deque<std::shared_ptr<CDemoEdit>>
	{
	public:
		bool m_FailAllocation = false;
		void push_back(const std::shared_ptr<CDemoEdit> &pJob)
		{
			if(m_FailAllocation)
				throw std::bad_alloc();
			std::deque<std::shared_ptr<CDemoEdit>>::push_back(pJob);
		}
	};

	class ReplaySaveSubmissionIntegration : public ::testing::Test
	{
	protected:
		CDemoStorageState m_State;
		CRecorderBoundary m_Recorder{m_State.m_Recorder};
		CTrackedReplayJobs m_Jobs;
		bool m_CreateThrows = false;
		bool m_CreateEmpty = false;
		bool m_EnqueueThrows = false;
		bool m_EnqueueRejects = false;
		bool m_RestartThrows = false;
		bool m_RestartFails = false;
		bool m_RunInline = false;
		int m_Creates = 0;
		int m_Enqueues = 0;
		int m_Restarts = 0;

		void SetUp() override
		{
			ASSERT_NE(m_State.m_pStorage, nullptr);
			CNetBase::Init();
			m_State.Begin();
			m_State.Write("target.demo", "previous");
		}

		replay_save::CResult Submit()
		{
			return replay_save::Submit(m_Recorder, m_Jobs, [&]() -> std::shared_ptr<CReplayJob> {
					++m_Creates;
					EXPECT_TRUE(m_State.m_Recorder.IsRecording());
					if(m_CreateThrows)
						throw std::bad_alloc();
					if(m_CreateEmpty)
						return nullptr;
					return std::make_shared<CReplayJob>("test", &*m_State.m_pDelta, &*m_State.m_pDeltaSixup, m_State.m_pStorage.get(), "source.demo", "target.demo", 100, 150); }, [&](const std::shared_ptr<CReplayJob> &pJob) {
					++m_Enqueues;
					EXPECT_FALSE(m_State.m_Recorder.IsRecording());
					EXPECT_EQ(m_Jobs.size(), 1U);
					EXPECT_EQ(m_State.Read("target.demo"), "previous");
					if(m_EnqueueThrows)
						throw std::bad_alloc();
					if(m_EnqueueRejects)
						return false;
					if(m_RunInline)
						pJob->RunNow();
					return true; }, [&]() {
					++m_Restarts;
					if(m_RestartThrows)
						throw 7;
					if(m_RestartFails)
						return false;
					if(!m_State.m_Recorder.IsRecording())
						m_State.Begin("restarted.demo");
					return m_State.m_Recorder.IsRecording(); });
		}
	};
}

TEST_F(ReplaySaveSubmissionIntegration, PreparationFailureKeepsOriginalRecordingActive)
{
	m_CreateThrows = true;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::PREPARATION_FAILED);
	EXPECT_TRUE(m_State.m_Recorder.IsRecording());
	EXPECT_EQ(m_Recorder.m_Stops, 0);
	EXPECT_EQ(m_Enqueues, 0);
	EXPECT_EQ(m_Restarts, 0);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(ReplaySaveSubmissionIntegration, EmptyFactoryResultDoesNotStopOrTrackARecording)
{
	m_CreateEmpty = true;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::PREPARATION_FAILED);
	EXPECT_EQ(m_Recorder.m_Stops, 0);
	EXPECT_TRUE(m_Jobs.empty());
}

TEST_F(ReplaySaveSubmissionIntegration, TrackerAllocationFailureHappensBeforeStoppingCapture)
{
	m_Jobs.m_FailAllocation = true;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::PREPARATION_FAILED);
	EXPECT_TRUE(m_State.m_Recorder.IsRecording());
	EXPECT_EQ(m_Recorder.m_Stops, 0);
	EXPECT_EQ(m_Restarts, 0);
}

TEST_F(ReplaySaveSubmissionIntegration, StopErrorDoesNotEnqueueOrReplaceOldTarget)
{
	m_Recorder.m_Failure = CRecorderBoundary::EFailure::RETURN_ERROR;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::STOP_FAILED);
	EXPECT_EQ(m_Enqueues, 0);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_TRUE(m_State.Exists("source.demo"));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
	EXPECT_EQ(m_Restarts, 1);
}

TEST_F(ReplaySaveSubmissionIntegration, ClosedRecorderWithWriteErrorCannotBeSubmitted)
{
	m_Recorder.m_Failure = CRecorderBoundary::EFailure::WRITE_ERROR;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::STOP_FAILED);
	EXPECT_EQ(m_Enqueues, 0);
	EXPECT_TRUE(m_State.Exists("source.demo"));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(ReplaySaveSubmissionIntegration, ApparentStopSuccessCannotSubmitAStillOpenRecorder)
{
	m_Recorder.m_Failure = CRecorderBoundary::EFailure::STILL_RECORDING;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::STOP_FAILED);
	EXPECT_TRUE(m_State.m_Recorder.IsRecording());
	EXPECT_EQ(m_Enqueues, 0);
}

TEST_F(ReplaySaveSubmissionIntegration, ThrowingStopDropsOnlyTheUnsubmittedObservation)
{
	m_Recorder.m_Failure = CRecorderBoundary::EFailure::THROW;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::STOP_FAILED);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_TRUE(m_State.m_Recorder.IsRecording());
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(ReplaySaveSubmissionIntegration, QueueRejectionKeepsSourceAndRestartsCapture)
{
	m_EnqueueRejects = true;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::QUEUE_FAILED);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_TRUE(m_State.Exists("source.demo"));
	EXPECT_TRUE(m_State.m_Recorder.IsRecording());
	EXPECT_STREQ(m_State.m_Recorder.CurrentFilename(), "restarted.demo");
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(ReplaySaveSubmissionIntegration, QueueExceptionKeepsSourceAndRemovesPendingObservation)
{
	m_EnqueueThrows = true;
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::QUEUE_FAILED);
	EXPECT_TRUE(m_Jobs.empty());
	EXPECT_TRUE(m_State.Exists("source.demo"));
	EXPECT_EQ(m_Restarts, 1);
}

TEST_F(ReplaySaveSubmissionIntegration, SubmittedJobRemainsObservedWhileNewCaptureStarts)
{
	EXPECT_EQ(Submit().m_Outcome, replay_save::EOutcome::SUBMITTED);
	EXPECT_EQ(m_Jobs.size(), 1U);
	EXPECT_EQ(m_Recorder.m_Stops, 1);
	EXPECT_EQ(m_Enqueues, 1);
	EXPECT_TRUE(m_State.m_Recorder.IsRecording());
	EXPECT_TRUE(m_State.Exists("source.demo"));
	EXPECT_EQ(m_State.Read("target.demo"), "previous");
}

TEST_F(ReplaySaveSubmissionIntegration, RestartFailureDoesNotUntrackAnAcceptedJob)
{
	m_RestartFails = true;
	const auto Result = Submit();
	EXPECT_EQ(Result.m_Outcome, replay_save::EOutcome::SUBMITTED);
	EXPECT_TRUE(Result.m_RestartFailed);
	EXPECT_EQ(m_Jobs.size(), 1U);
	EXPECT_TRUE(m_State.Exists("source.demo"));
}

TEST_F(ReplaySaveSubmissionIntegration, RestartExceptionDoesNotUndoAlreadyCompletedOutput)
{
	m_RunInline = true;
	m_RestartThrows = true;
	const auto Result = Submit();
	EXPECT_EQ(Result.m_Outcome, replay_save::EOutcome::SUBMITTED);
	EXPECT_TRUE(Result.m_RestartFailed);
	EXPECT_EQ(m_Jobs.size(), 1U);
	EXPECT_EQ(m_State.MapData("target.demo"), "mapbytes");
	EXPECT_FALSE(m_State.Exists("source.demo"));
}

TEST_F(ReplaySaveSubmissionIntegration, RejectedJobCanBeRetriedFromTheRetainedSource)
{
	m_EnqueueRejects = true;
	ASSERT_EQ(Submit().m_Outcome, replay_save::EOutcome::QUEUE_FAILED);
	CReplayJob Retry("test", &*m_State.m_pDelta, &*m_State.m_pDeltaSixup, m_State.m_pStorage.get(), "source.demo", "target.demo", 100, 150);
	Retry.RunNow();
	EXPECT_TRUE(Retry.Success());
	EXPECT_EQ(m_State.MapData("target.demo"), "mapbytes");
}
