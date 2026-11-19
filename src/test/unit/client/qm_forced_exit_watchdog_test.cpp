#include <engine/client/qm_forced_exit_watchdog.h>

#include <gtest/gtest.h>

#include <array>
#include <new>

using namespace std::chrono_literals;

namespace
{
	struct CBackend
	{
		struct SPending
		{
			QmForcedExit::FEntry m_pfnEntry = nullptr;
			void *m_pUser = nullptr;
		};
		std::array<SPending, 16> m_aPending{};
		bool m_Reject = false;
		bool m_Inline = false;
		bool m_ThrowWait = false;
		bool m_StopOnLaunch = false;
		int m_Launches = 0;
		QmForcedExit::CWatchdog *m_pOwner = nullptr;
		static bool Launch(void *pContext, QmForcedExit::FEntry Entry, void *pUser) noexcept
		{
			auto &Self = *static_cast<CBackend *>(pContext);
			++Self.m_Launches;
			if(Self.m_Reject)
				return false;
			if(Self.m_StopOnLaunch)
				Self.m_pOwner->Stop();
			if(Self.m_Inline)
			{
				Entry(pUser);
				return true;
			}
			for(auto &Pending : Self.m_aPending)
			{
				if(Pending.m_pfnEntry)
					continue;
				Pending = {Entry, pUser};
				return true;
			}
			return false;
		}
		static void Wait(void *pContext, std::chrono::milliseconds)
		{
			if(static_cast<CBackend *>(pContext)->m_ThrowWait)
				throw 7;
		}
		void Drain()
		{
			for(auto &Pending : m_aPending)
			{
				const auto Entry = Pending;
				Pending = {};
				if(Entry.m_pfnEntry)
					Entry.m_pfnEntry(Entry.m_pUser);
			}
		}
	};

	class QmForcedExitWatchdog : public testing::Test
	{
	protected:
		CBackend m_Backend;
		QmForcedExit::CWatchdog m_Watchdog;
		QmForcedExit::SOperations m_Operations{&m_Backend, CBackend::Launch, CBackend::Wait};
		int m_Fired = 0;
		void SetUp() override { m_Backend.m_pOwner = &m_Watchdog; }
		void TearDown() override
		{
			m_Watchdog.Stop();
			m_Backend.Drain();
		}
		bool Start()
		{
			return m_Watchdog.Start(10s, [this] { ++m_Fired; }, m_Operations);
		}
	};

	struct CThrowOnCopy
	{
		CThrowOnCopy() = default;
		CThrowOnCopy(const CThrowOnCopy &) { throw std::bad_alloc(); }
		void operator()() const {}
	};
}

TEST_F(QmForcedExitWatchdog, RejectedLaunchReleasesCandidateAndAllowsRetry)
{
	std::weak_ptr<QmForcedExit::CWatchdog::SState> Candidate;
	m_Backend.m_Reject = true;
	EXPECT_FALSE(m_Watchdog.Start(10s, [] {}, m_Operations, [&](uint64_t Epoch) {
		auto State = std::make_shared<QmForcedExit::CWatchdog::SState>(Epoch);
		Candidate = State;
		return State; }));
	EXPECT_TRUE(Candidate.expired());
	EXPECT_FALSE(m_Watchdog.Active());
	m_Backend.m_Reject = false;
	ASSERT_TRUE(Start());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(QmForcedExitWatchdog, AllocationFailurePreparingReplacementPreservesExistingWatchdog)
{
	ASSERT_TRUE(Start());
	EXPECT_FALSE(m_Watchdog.Start(10s, [] {}, m_Operations, [](uint64_t) -> std::shared_ptr<QmForcedExit::CWatchdog::SState> { throw std::bad_alloc(); }));
	EXPECT_TRUE(m_Watchdog.Active());
	EXPECT_EQ(m_Backend.m_Launches, 1);
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(QmForcedExitWatchdog, UnknownPreparationExceptionDoesNotEscapeOrArm)
{
	EXPECT_FALSE(m_Watchdog.Start(10s, [] {}, m_Operations, [](uint64_t) -> std::shared_ptr<QmForcedExit::CWatchdog::SState> { throw 7; }));
	EXPECT_FALSE(m_Watchdog.Active());
	EXPECT_EQ(m_Backend.m_Launches, 0);
}

TEST_F(QmForcedExitWatchdog, NullCandidateIsNotSubmitted)
{
	EXPECT_FALSE(m_Watchdog.Start(10s, [] {}, m_Operations, [](uint64_t) { return std::shared_ptr<QmForcedExit::CWatchdog::SState>{}; }));
	EXPECT_EQ(m_Backend.m_Launches, 0);
	EXPECT_FALSE(m_Watchdog.Active());
}

TEST_F(QmForcedExitWatchdog, EmptyExitCallbackDoesNotCreateThread)
{
	EXPECT_FALSE(m_Watchdog.Start(10s, std::function<void()>{}, m_Operations));
	EXPECT_EQ(m_Backend.m_Launches, 0);
}

TEST_F(QmForcedExitWatchdog, CallbackCopyFailurePreservesExistingWatchdog)
{
	ASSERT_TRUE(Start());
	CThrowOnCopy Callback;
	EXPECT_FALSE(m_Watchdog.Start(10s, Callback, m_Operations));
	EXPECT_TRUE(m_Watchdog.Active());
	EXPECT_EQ(m_Backend.m_Launches, 1);
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(QmForcedExitWatchdog, StopIsIdempotentAndPreventsPendingExit)
{
	EXPECT_FALSE(m_Watchdog.Stop());
	ASSERT_TRUE(Start());
	EXPECT_TRUE(m_Watchdog.Stop());
	EXPECT_FALSE(m_Watchdog.Stop());
	EXPECT_FALSE(m_Watchdog.Active());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 0);
}

TEST_F(QmForcedExitWatchdog, SuccessfulReplacementCancelsOnlyThePreviousRun)
{
	ASSERT_TRUE(m_Watchdog.Start(10s, [this] { m_Fired += 100; }, m_Operations));
	ASSERT_TRUE(Start());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 1);
	EXPECT_FALSE(m_Watchdog.Active());
}

TEST_F(QmForcedExitWatchdog, RejectedReplacementKeepsPreviousRunArmed)
{
	ASSERT_TRUE(Start());
	m_Backend.m_Reject = true;
	EXPECT_FALSE(m_Watchdog.Start(10s, [this] { m_Fired += 100; }, m_Operations));
	EXPECT_TRUE(m_Watchdog.Active());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(QmForcedExitWatchdog, StopDuringAcceptedLaunchPreventsItsLaterExit)
{
	m_Backend.m_StopOnLaunch = true;
	EXPECT_FALSE(Start());
	EXPECT_FALSE(m_Watchdog.Active());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 0);
}

TEST_F(QmForcedExitWatchdog, StopDuringPreparationPreventsSubmission)
{
	EXPECT_FALSE(m_Watchdog.Start(10s, [this] { ++m_Fired; }, m_Operations, [&](uint64_t Epoch) {
		m_Watchdog.Stop();
		return std::make_shared<QmForcedExit::CWatchdog::SState>(Epoch); }));
	EXPECT_EQ(m_Backend.m_Launches, 0);
	EXPECT_FALSE(m_Watchdog.Active());
}

TEST_F(QmForcedExitWatchdog, WaitExceptionDoesNotEscapeTheWorkerEntry)
{
	m_Backend.m_ThrowWait = true;
	ASSERT_TRUE(Start());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 0);
	EXPECT_FALSE(m_Watchdog.Active());
}

TEST_F(QmForcedExitWatchdog, ExitCallbackExceptionIsContainedAndLaterStartCanRecover)
{
	ASSERT_TRUE(m_Watchdog.Start(10s, [] { throw 7; }, m_Operations));
	m_Backend.Drain();
	EXPECT_FALSE(m_Watchdog.Active());
	ASSERT_TRUE(Start());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(QmForcedExitWatchdog, InvalidOperationsDoNotReplaceAnExistingWatchdog)
{
	ASSERT_TRUE(Start());
	EXPECT_FALSE(m_Watchdog.Start(-1ms, [] {}, m_Operations));
	auto Invalid = m_Operations;
	Invalid.m_pfnLaunch = nullptr;
	EXPECT_FALSE(m_Watchdog.Start(10s, [] {}, Invalid));
	Invalid = m_Operations;
	Invalid.m_pfnWait = nullptr;
	EXPECT_FALSE(m_Watchdog.Start(10s, [] {}, Invalid));
	EXPECT_TRUE(m_Watchdog.Active());
	EXPECT_EQ(m_Backend.m_Launches, 1);
}

TEST_F(QmForcedExitWatchdog, PendingRunOwnsItsStateAfterWatchdogDestruction)
{
	std::weak_ptr<QmForcedExit::CWatchdog::SState> State;
	{
		QmForcedExit::CWatchdog Owner;
		ASSERT_TRUE(Owner.Start(10s, [this] { ++m_Fired; }, m_Operations, [&](uint64_t Epoch) {
			auto Candidate = std::make_shared<QmForcedExit::CWatchdog::SState>(Epoch);
			State = Candidate;
			return Candidate; }));
	}
	EXPECT_FALSE(State.expired());
	m_Backend.Drain();
	EXPECT_EQ(m_Fired, 0);
	EXPECT_TRUE(State.expired());
}

TEST_F(QmForcedExitWatchdog, WorkerCanCompleteBeforeLaunchReturns)
{
	m_Backend.m_Inline = true;
	ASSERT_TRUE(Start());
	EXPECT_EQ(m_Fired, 1);
	EXPECT_FALSE(m_Watchdog.Active());
}
