#include <base/str.h>

#include <game/client/QmUi/cards/QmSteamProbeState.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <chrono>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>

namespace
{
	SSteamClientInfo Inspect(const char *pPath)
	{
		SSteamClientInfo Info;
		str_copy(Info.m_aPath, pPath, sizeof(Info.m_aPath));
		Info.m_Source = ESteamClientSource::MANUAL;
		Info.m_Running = true;
		Info.m_RunningKnown = true;
		return Info;
	}

	SSteamClientInfo FailInspect(const char *) { throw std::bad_alloc(); }
	SSteamClientInfo EmptyInspect(const char *) { return {}; }

	class CAbortableProbe : public CSteamProbeJob
	{
	public:
		explicit CAbortableProbe(const char *pPath) :
			CSteamProbeJob(pPath, Inspect)
		{
			Abortable(true);
		}
	};

	struct CHarness
	{
		CSteamProbeState m_State;
		CJobPool m_Pool;
		std::shared_ptr<CSteamProbeJob> m_pJob;
		int m_Submissions = 0;

		bool Start(const char *pPath, bool Automatic = true, SSteamClientInfo (*pfnInspect)(const char *) = Inspect)
		{
			return m_State.TryStart(pPath, Automatic, [pfnInspect](const char *pValue) { return std::make_shared<CSteamProbeJob>(pValue, pfnInspect); }, [this](const auto &pJob) {
					++m_Submissions;
					m_pJob = pJob;
					return m_Pool.TryAdd(pJob); });
		}

		bool Finish(const char *pPath)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			while(m_pJob && !m_pJob->Done() && std::chrono::steady_clock::now() < Deadline)
				std::this_thread::yield();
			if(!m_pJob || !m_pJob->Done())
				return false;
			m_State.Finish(pPath);
			return true;
		}
	};
}

TEST(SteamProbeState, CompletedProbePublishesMatchingResultAndDoesNotRepeat)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		ASSERT_TRUE(p->Finish("old"));
		EXPECT_TRUE(p->m_State.HasResult());
		EXPECT_FALSE(p->m_State.Pending());
		EXPECT_STREQ(p->m_State.Info().m_aPath, "old");
		EXPECT_TRUE(p->m_State.Info().m_RunningKnown);
		EXPECT_FALSE(p->Start("old"));
		EXPECT_EQ(p->m_Submissions, 1);
	}));
}

TEST(SteamProbeState, FactoryAllocationFailureKeepsOldResultAndExplicitRefresh)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		ASSERT_TRUE(p->Finish("old"));
		p->m_State.RequestRefresh();
		EXPECT_FALSE(p->m_State.TryStart("old", false, [](const char *) -> std::shared_ptr<CSteamProbeJob> { throw std::bad_alloc(); }, [](const auto &) { ADD_FAILURE(); return true; }));
		EXPECT_FALSE(p->m_State.Pending());
		EXPECT_STREQ(p->m_State.Info().m_aPath, "old");
		ASSERT_TRUE(p->Start("old", false));
		ASSERT_TRUE(p->Finish("old"));
	}));
}

TEST(SteamProbeState, PathConstructionExceptionLeavesInitialRequestRetryable)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		EXPECT_FALSE(p->m_State.TryStart("new", true, [](const char *) -> std::shared_ptr<CSteamProbeJob> { throw std::length_error("path"); }, [](const auto &) { ADD_FAILURE(); return true; }));
		EXPECT_FALSE(p->m_State.Pending());
		EXPECT_FALSE(p->m_State.HasResult());
		ASSERT_TRUE(p->Start("new"));
		ASSERT_TRUE(p->Finish("new"));
		EXPECT_STREQ(p->m_State.Info().m_aPath, "new");
	}));
}

TEST(SteamProbeState, ClosedPoolDoesNotPublishWaitingIdentityOrConsumeRefresh)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		ASSERT_TRUE(p->Finish("old"));
		p->m_Pool.Shutdown();
		p->m_State.RequestRefresh();
		EXPECT_FALSE(p->Start("old", false));
		EXPECT_FALSE(p->m_State.Pending());
		EXPECT_STREQ(p->m_State.Info().m_aPath, "old");
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old", false));
		ASSERT_TRUE(p->Finish("old"));
	}));
}

TEST(SteamProbeState, SubmissionExceptionPreservesRefreshForNextAttempt)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		p->m_State.RequestRefresh();
		EXPECT_FALSE(p->m_State.TryStart("new", false, [](const char *pPath) { return std::make_shared<CSteamProbeJob>(pPath, Inspect); }, [](const auto &) -> bool { throw std::bad_alloc(); }));
		EXPECT_FALSE(p->m_State.Pending());
		ASSERT_TRUE(p->Start("new", false));
		ASSERT_TRUE(p->Finish("new"));
	}));
}

TEST(SteamProbeState, WorkerExceptionDoesNotPublishEmptyInfoAndRetriesRefresh)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		ASSERT_TRUE(p->Finish("old"));
		p->m_State.RequestRefresh();
		ASSERT_TRUE(p->Start("old", false, FailInspect));
		ASSERT_TRUE(p->Finish("old"));
		EXPECT_TRUE(p->m_State.HasResult());
		EXPECT_STREQ(p->m_State.Info().m_aPath, "old");
		EXPECT_TRUE(p->m_State.Info().m_Running);
		ASSERT_TRUE(p->Start("old", false));
		ASSERT_TRUE(p->Finish("old"));
	}));
}

TEST(SteamProbeState, StaleCompletionKeepsOldResultUntilEditingEnds)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		ASSERT_TRUE(p->Finish("old"));
		ASSERT_TRUE(p->Start("outdated"));
		ASSERT_TRUE(p->Finish("current"));
		EXPECT_STREQ(p->m_State.Info().m_aPath, "old");
		EXPECT_FALSE(p->Start("current", false));
		ASSERT_TRUE(p->Start("current"));
		ASSERT_TRUE(p->Finish("current"));
		EXPECT_STREQ(p->m_State.Info().m_aPath, "current");
	}));
}

TEST(SteamProbeState, PendingTaskSuppressesDuplicateSubmissionUntilCollected)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		EXPECT_FALSE(p->Start("old"));
		EXPECT_FALSE(p->Start("changed"));
		EXPECT_EQ(p->m_Submissions, 1);
		ASSERT_TRUE(p->Finish("old"));
	}));
}

TEST(SteamProbeState, ExplicitRefreshCanStartWhileAutomaticDetectionIsSuppressed)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		EXPECT_FALSE(p->Start("editing", false));
		p->m_State.RequestRefresh();
		ASSERT_TRUE(p->Start("editing", false));
		ASSERT_TRUE(p->Finish("editing"));
	}));
}

TEST(SteamProbeState, SuccessfulEmptyProbeIsAResultRatherThanAutomaticRetry)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("missing", true, EmptyInspect));
		ASSERT_TRUE(p->Finish("missing"));
		EXPECT_TRUE(p->m_State.HasResult());
		EXPECT_STREQ(p->m_State.Info().m_aPath, "");
		EXPECT_FALSE(p->Start("missing"));
	}));
}

TEST(SteamProbeState, AbortedJobPreservesPreviousResultAndRestoresRefresh)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		auto p = std::make_shared<CHarness>();
		ASSERT_TRUE(p->m_Pool.TryInit(1));
		ASSERT_TRUE(p->Start("old"));
		ASSERT_TRUE(p->Finish("old"));
		p->m_State.RequestRefresh();
		ASSERT_TRUE(p->m_State.TryStart("old", false, [](const char *pPath) {
				auto pJob = std::make_shared<CAbortableProbe>(pPath);
				EXPECT_TRUE(pJob->Abort());
				return pJob; }, [p](const auto &pJob) {
				p->m_pJob = pJob;
				return p->m_Pool.TryAdd(pJob); }));
		ASSERT_TRUE(p->Finish("old"));
		EXPECT_FALSE(p->m_State.Pending());
		EXPECT_STREQ(p->m_State.Info().m_aPath, "old");
		ASSERT_TRUE(p->Start("old", false));
		ASSERT_TRUE(p->Finish("old"));
	}));
}
