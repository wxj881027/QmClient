#include <engine/client/demoedit.h>
#include <engine/client/replay_save.h>
#include <engine/shared/config.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>
#include <test/support/demo_storage_state.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
	class CReplayJobForTest : public CDemoEdit
	{
	public:
		using CDemoEdit::CDemoEdit;
		void RunNow() { Run(); }
	};

	class CJobFileOperations : public CDemoEdit::COperations
	{
	public:
		bool m_PublishFails = false;
		bool m_PublishThrows = false;
		bool m_RemoveSourceFails = false;
		bool m_RemoveSourceThrows = false;
		int m_SourceRemoves = 0;

		bool Publish(const char *pTemporary, const char *pDestination) override
		{
			if(m_PublishThrows)
				throw std::bad_alloc();
			return !m_PublishFails && COperations::Publish(pTemporary, pDestination);
		}
		bool RemoveSource(IStorage &Storage, const char *pPath) override
		{
			++m_SourceRemoves;
			if(m_RemoveSourceThrows)
				throw 7;
			return !m_RemoveSourceFails && COperations::RemoveSource(Storage, pPath);
		}
	};

	struct CReplayJobState
	{
		CDemoStorageState m_Data;
		CJobFileOperations m_Operations;
		std::shared_ptr<CReplayJobForTest> m_pJob;
		bool m_Accepted = false;
		void Prepare()
		{
			m_Data.Create();
			m_Data.Write("target.demo", "previous");
			m_pJob = std::make_shared<CReplayJobForTest>("test", &*m_Data.m_pDelta, &*m_Data.m_pDeltaSixup, m_Data.m_pStorage.get(), "source.demo", "target.demo", 100, 150, &m_Operations);
		}
	};

	class DemoEditJobIntegration : public ::testing::Test
	{
	protected:
		std::shared_ptr<CReplayJobState> m_pState = std::make_shared<CReplayJobState>();
		int m_SliceBegin = -1;
		int m_SliceEnd = -1;
		void SetUp() override
		{
			m_SliceBegin = g_Config.m_ClDemoSliceBegin;
			m_SliceEnd = g_Config.m_ClDemoSliceEnd;
			ASSERT_NE(m_pState->m_Data.m_pStorage, nullptr);
			CNetBase::Init();
			m_pState->Prepare();
		}
		void TearDown() override
		{
			g_Config.m_ClDemoSliceBegin = m_SliceBegin;
			g_Config.m_ClDemoSliceEnd = m_SliceEnd;
		}
		::testing::AssertionResult RunOnWorker()
		{
			const auto pState = m_pState;
			return WaitForAsyncIo([pState] {
				CJobPool Pool;
				if(!Pool.TryInit(1))
					return;
				pState->m_Accepted = Pool.TryAdd(pState->m_pJob);
				Pool.Shutdown();
			});
		}
	};
}

TEST_F(DemoEditJobIntegration, NewlyPreparedJobStartsWithAnExplicitFailureResult)
{
	EXPECT_FALSE(m_pState->m_pJob->Success());
	EXPECT_EQ(m_pState->m_pJob->State(), IJob::STATE_QUEUED);
	EXPECT_EQ(m_pState->m_Data.Read("target.demo"), "previous");
}

TEST_F(DemoEditJobIntegration, FailedInputPreservesBothTheSourceAndOldTarget)
{
	m_pState->m_Data.Write("source.demo", "broken source");
	m_pState->m_pJob->RunNow();
	EXPECT_FALSE(m_pState->m_pJob->Success());
	EXPECT_EQ(m_pState->m_Data.Read("source.demo"), "broken source");
	EXPECT_EQ(m_pState->m_Data.Read("target.demo"), "previous");
	EXPECT_EQ(m_pState->m_Operations.m_SourceRemoves, 0);
}

TEST_F(DemoEditJobIntegration, SourceIsRemovedOnlyAfterACompleteReplacement)
{
	m_pState->m_pJob->RunNow();
	EXPECT_TRUE(m_pState->m_pJob->Success());
	EXPECT_FALSE(m_pState->m_Data.Exists("source.demo"));
	EXPECT_EQ(m_pState->m_Data.MapData("target.demo"), "mapbytes");
	EXPECT_EQ(m_pState->m_Operations.m_SourceRemoves, 1);
}

TEST_F(DemoEditJobIntegration, PublishingFailureDoesNotRemoveTheCapturedSource)
{
	m_pState->m_Operations.m_PublishFails = true;
	m_pState->m_pJob->RunNow();
	EXPECT_FALSE(m_pState->m_pJob->Success());
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
	EXPECT_EQ(m_pState->m_Data.Read("target.demo"), "previous");
	EXPECT_EQ(m_pState->m_Operations.m_SourceRemoves, 0);
}

TEST_F(DemoEditJobIntegration, PublishingExceptionBecomesAnExplicitFailureResult)
{
	m_pState->m_Operations.m_PublishThrows = true;
	m_pState->m_pJob->RunNow();
	EXPECT_FALSE(m_pState->m_pJob->Success());
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
	EXPECT_EQ(m_pState->m_Data.Read("target.demo"), "previous");
}

TEST_F(DemoEditJobIntegration, CleanupFailureKeepsTheAlreadyPublishedSuccess)
{
	m_pState->m_Operations.m_RemoveSourceFails = true;
	m_pState->m_pJob->RunNow();
	EXPECT_TRUE(m_pState->m_pJob->Success());
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
	EXPECT_EQ(m_pState->m_Data.MapData("target.demo"), "mapbytes");
}

TEST_F(DemoEditJobIntegration, CleanupExceptionDoesNotEraseThePublishedSuccess)
{
	m_pState->m_Operations.m_RemoveSourceThrows = true;
	m_pState->m_pJob->RunNow();
	EXPECT_TRUE(m_pState->m_pJob->Success());
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
	EXPECT_EQ(m_pState->m_Data.MapData("target.demo"), "mapbytes");
}

TEST_F(DemoEditJobIntegration, AClosedJobPoolRejectsTheJobWithoutTouchingFiles)
{
	CJobPool Pool;
	EXPECT_FALSE(Pool.TryAdd(m_pState->m_pJob));
	EXPECT_FALSE(m_pState->m_pJob->Success());
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
	EXPECT_EQ(m_pState->m_Data.Read("target.demo"), "previous");
}

TEST_F(DemoEditJobIntegration, RealWorkerCompletesBeforePoolShutdownReleasesStorage)
{
	const auto pState = m_pState;
	ASSERT_TRUE(RunOnWorker());
	EXPECT_TRUE(pState->m_Accepted);
	EXPECT_EQ(pState->m_pJob->State(), IJob::STATE_DONE);
	EXPECT_TRUE(pState->m_pJob->Success());
	EXPECT_EQ(pState->m_Data.MapData("target.demo"), "mapbytes");
}

TEST_F(DemoEditJobIntegration, BackgroundExportDoesNotOverwriteMainThreadSelection)
{
	g_Config.m_ClDemoSliceBegin = 111;
	g_Config.m_ClDemoSliceEnd = 144;
	const auto pState = m_pState;
	ASSERT_TRUE(RunOnWorker());
	EXPECT_TRUE(pState->m_Accepted);
	EXPECT_TRUE(pState->m_pJob->Success());
	EXPECT_EQ(g_Config.m_ClDemoSliceBegin, 111);
	EXPECT_EQ(g_Config.m_ClDemoSliceEnd, 144);
}

TEST_F(DemoEditJobIntegration, JobOwnsClonedDeltasAfterOriginalsAreReplaced)
{
	m_pState->m_Data.m_pDelta = CSnapshotDelta::New();
	m_pState->m_Data.m_pDeltaSixup = CSnapshotDelta::New();
	m_pState->m_pJob->RunNow();
	EXPECT_TRUE(m_pState->m_pJob->Success());
	EXPECT_EQ(m_pState->m_Data.MapData("target.demo"), "mapbytes");
}

TEST_F(DemoEditJobIntegration, SourceAndDestinationAliasAreRejectedBeforeAnyReplacement)
{
	auto &Data = m_pState->m_Data;
	EXPECT_THROW((CReplayJobForTest("test", &*Data.m_pDelta, &*Data.m_pDeltaSixup, Data.m_pStorage.get(), "source.demo", "./source.demo", 100, 150)), std::invalid_argument);
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
}

TEST_F(DemoEditJobIntegration, DirectoryDestinationIsRejectedDuringPreparation)
{
	ASSERT_TRUE(m_pState->m_Data.m_pStorage->CreateFolder("directory.demo", IStorage::TYPE_SAVE));
	auto &Data = m_pState->m_Data;
	EXPECT_THROW((CReplayJobForTest("test", &*Data.m_pDelta, &*Data.m_pDeltaSixup, Data.m_pStorage.get(), "source.demo", "directory.demo", 100, 150)), std::invalid_argument);
	EXPECT_TRUE(m_pState->m_Data.Exists("source.demo"));
}

TEST_F(DemoEditJobIntegration, PendingJobIsNotConsumedOrNotified)
{
	std::deque<std::shared_ptr<CDemoEdit>> Jobs{m_pState->m_pJob};
	int Notifications = 0;
	EXPECT_FALSE(replay_save::ConsumeCompleted(Jobs, [&](const auto &) { ++Notifications; }));
	EXPECT_EQ(Jobs.size(), 1U);
	EXPECT_EQ(Notifications, 0);
}

TEST_F(DemoEditJobIntegration, CompletedResultIsConsumedBeforeAThrowingNotification)
{
	ASSERT_TRUE(RunOnWorker());
	ASSERT_EQ(m_pState->m_pJob->State(), IJob::STATE_DONE);
	std::deque<std::shared_ptr<CDemoEdit>> Jobs{m_pState->m_pJob};
	int Notifications = 0;
	const auto Notify = [&](const auto &) {
		++Notifications;
		EXPECT_TRUE(Jobs.empty());
		throw std::bad_alloc();
	};
	EXPECT_TRUE(replay_save::ConsumeCompleted(Jobs, Notify));
	EXPECT_FALSE(replay_save::ConsumeCompleted(Jobs, Notify));
	EXPECT_EQ(Notifications, 1);
}

TEST_F(DemoEditJobIntegration, UnknownNotificationExceptionDoesNotRequeueTheResult)
{
	ASSERT_TRUE(RunOnWorker());
	ASSERT_EQ(m_pState->m_pJob->State(), IJob::STATE_DONE);
	std::deque<std::shared_ptr<CDemoEdit>> Jobs{m_pState->m_pJob};
	EXPECT_TRUE(replay_save::ConsumeCompleted(Jobs, [](const auto &) { throw 7; }));
	EXPECT_TRUE(Jobs.empty());
}
