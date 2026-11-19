#include <engine/client/perf_file_logger.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace
{
	class CObservedPreparation : public CQmPerfFileSwitchLogger::CPreparationOperations
	{
	public:
		bool m_FailJob = false;
		bool m_EmptyJob = false;
		bool m_FailReserve = false;
		int m_Jobs = 0;
		int m_Reserves = 0;
		std::shared_ptr<CQmPerfLoggerCloseJob> CreateCloseJob() override
		{
			++m_Jobs;
			if(m_FailJob)
				throw std::bad_alloc();
			if(m_EmptyJob)
				return nullptr;
			return CPreparationOperations::CreateCloseJob();
		}
		void Reserve(std::vector<std::shared_ptr<CQmPerfLoggerCloseJob>> &vpJobs, size_t Count) override
		{
			++m_Reserves;
			if(m_FailReserve)
				throw std::bad_alloc();
			CPreparationOperations::Reserve(vpJobs, Count);
		}
	};

	struct CProbeData
	{
		std::atomic<int> m_Logs{0};
		std::atomic<int> m_Destroyed{0};
	};

	class CProbeLogger : public ILogger
	{
		std::shared_ptr<CProbeData> m_pData;
		std::function<void()> m_OnDestroy;

	public:
		explicit CProbeLogger(std::shared_ptr<CProbeData> pData, std::function<void()> OnDestroy = {}) :
			m_pData(std::move(pData)), m_OnDestroy(std::move(OnDestroy)) {}
		~CProbeLogger() override
		{
			if(m_OnDestroy)
				m_OnDestroy();
			++m_pData->m_Destroyed;
		}
		void Log(const CLogMessage *) override { ++m_pData->m_Logs; }
	};

	struct CState
	{
		CObservedPreparation m_Operations;
		CQmPerfFileSwitchLogger m_Switch{&m_Operations};
		std::shared_ptr<CProbeData> m_pOld = std::make_shared<CProbeData>();
		std::shared_ptr<CProbeData> m_pNext = std::make_shared<CProbeData>();
		void Start() { m_Switch.Set(std::make_shared<CProbeLogger>(m_pOld)); }
		void Log()
		{
			CLogMessage Msg{};
			m_Switch.Log(&Msg);
		}
	};
}

TEST(PerfFileSwitch, FailedSynchronousReplacementKeepsTheOldBackend)
{
	CState State;
	State.Start();
	State.m_Operations.m_FailJob = true;
	EXPECT_THROW(State.m_Switch.Set(std::make_shared<CProbeLogger>(State.m_pNext)), std::bad_alloc);
	State.Log();
	EXPECT_EQ(State.m_pOld->m_Logs.load(), 1);
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 0);
	EXPECT_EQ(State.m_pNext->m_Logs.load(), 0);
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, FailedCloseJobPreparationDoesNotPublishTheAsyncReplacement)
{
	CState State;
	State.Start();
	State.m_Operations.m_FailJob = true;
	EXPECT_THROW(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), std::bad_alloc);
	State.Log();
	EXPECT_EQ(State.m_pOld->m_Logs.load(), 1);
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 0);
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, FailedQueueGrowthKeepsTheOldBackendAndReleasesTheCandidate)
{
	CState State;
	State.Start();
	State.m_Operations.m_FailReserve = true;
	EXPECT_THROW(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), std::bad_alloc);
	State.Log();
	EXPECT_EQ(State.m_pOld->m_Logs.load(), 1);
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 0);
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, NullCloseJobPreparationIsRejectedBeforePublication)
{
	CState State;
	State.Start();
	State.m_Operations.m_EmptyJob = true;
	EXPECT_THROW(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), std::bad_alloc);
	State.Log();
	EXPECT_EQ(State.m_pOld->m_Logs.load(), 1);
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, FailedInitialReservationLeavesTheSwitchEmpty)
{
	CState State;
	State.m_Operations.m_FailReserve = true;
	EXPECT_THROW(State.Start(), std::bad_alloc);
	State.Log();
	EXPECT_EQ(State.m_pOld->m_Logs.load(), 0);
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, DisablingUsesTheAlreadyPreparedCloseJobAndCapacity)
{
	CState State;
	State.Start();
	const int Jobs = State.m_Operations.m_Jobs;
	const int Reserves = State.m_Operations.m_Reserves;
	State.m_Operations.m_FailJob = State.m_Operations.m_FailReserve = true;
	auto pClose = State.m_Switch.SetAsync(nullptr);
	ASSERT_NE(pClose, nullptr);
	State.Log();
	EXPECT_EQ(State.m_pOld->m_Logs.load(), 0);
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 0);
	EXPECT_EQ(State.m_Operations.m_Jobs, Jobs);
	EXPECT_EQ(State.m_Operations.m_Reserves, Reserves);
	State.m_Switch.FinishPending();
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, RepeatedDisableDoesNotCreateAnotherCloseJob)
{
	CState State;
	State.Start();
	auto pClose = State.m_Switch.SetAsync(nullptr);
	ASSERT_NE(pClose, nullptr);
	EXPECT_EQ(State.m_Switch.SetAsync(nullptr), nullptr);
	State.m_Switch.FinishPending();
	State.m_Switch.FinishPending();
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, StartingFromEmptyPreparesTheFutureCloseWithoutReturningWork)
{
	CState State;
	EXPECT_EQ(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), nullptr);
	State.Log();
	EXPECT_EQ(State.m_pNext->m_Logs.load(), 1);
	EXPECT_NE(State.m_Switch.SetAsync(nullptr), nullptr);
	State.m_Switch.FinishPending();
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, FailedReplacementCanBeRetried)
{
	CState State;
	State.Start();
	State.m_Operations.m_FailReserve = true;
	EXPECT_THROW(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), std::bad_alloc);
	State.m_Operations.m_FailReserve = false;
	ASSERT_NE(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), nullptr);
	State.Log();
	EXPECT_EQ(State.m_pNext->m_Logs.load(), 1);
	State.m_Switch.FinishPending();
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 1);
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, DrainingOldJobsKeepsTheReservedCapacityForTheActiveBackend)
{
	CState State;
	State.Start();
	ASSERT_NE(State.m_Switch.SetAsync(std::make_shared<CProbeLogger>(State.m_pNext)), nullptr);
	State.m_Switch.FinishPending();
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 1);
	State.m_Operations.m_FailJob = State.m_Operations.m_FailReserve = true;
	EXPECT_NE(State.m_Switch.SetAsync(nullptr), nullptr);
	State.m_Switch.FinishPending();
	EXPECT_EQ(State.m_pNext->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, ClosingAnOldBackendDoesNotHoldTheSwitchLock)
{
	auto pState = std::make_shared<CState>();
	CQmPerfFileSwitchLogger *pSwitch = &pState->m_Switch;
	pState->m_Switch.Set(std::make_shared<CProbeLogger>(pState->m_pOld, [pSwitch] {
		CLogMessage Msg{};
		pSwitch->Log(&Msg);
	}));
	ASSERT_NE(pState->m_Switch.SetAsync(std::make_shared<CProbeLogger>(pState->m_pNext)), nullptr);
	ASSERT_TRUE(WaitForAsyncIo([pState] { pState->m_Switch.FinishPending(); }));
	EXPECT_EQ(pState->m_pOld->m_Destroyed.load(), 1);
	EXPECT_EQ(pState->m_pNext->m_Logs.load(), 1);
}

TEST(PerfFileSwitch, ShutdownDrainsAnUndispatchedCloseOnlyOnce)
{
	CState State;
	State.Start();
	auto pClose = State.m_Switch.SetAsync(nullptr);
	ASSERT_NE(pClose, nullptr);
	State.m_Switch.GlobalFinish();
	State.m_Switch.GlobalFinish();
	pClose.reset();
	EXPECT_EQ(State.m_pOld->m_Destroyed.load(), 1);
}

TEST(PerfFileSwitch, ConcurrentFinishWaitsForTheSameCloseToComplete)
{
	struct CGate
	{
		std::mutex m_Mutex;
		std::condition_variable m_Cv;
		bool m_Entered = false;
		bool m_Release = false;
		bool m_TimedOut = false;
		bool m_SecondStarted = false;
		bool m_SecondFinished = false;
		bool m_EnteredInTime = false;
		bool m_StartedInTime = false;
		bool m_ReturnedEarly = false;
	};
	auto pState = std::make_shared<CState>();
	auto pGate = std::make_shared<CGate>();
	pState->m_Switch.Set(std::make_shared<CProbeLogger>(pState->m_pOld, [pGate] {
		std::unique_lock<std::mutex> Lock(pGate->m_Mutex);
		pGate->m_Entered = true;
		pGate->m_Cv.notify_all();
		pGate->m_TimedOut = !pGate->m_Cv.wait_for(Lock, std::chrono::seconds(2), [&] { return pGate->m_Release; });
	}));
	ASSERT_NE(pState->m_Switch.SetAsync(nullptr), nullptr);
	ASSERT_TRUE(WaitForAsyncIo([pState, pGate] {
		std::thread First([pState] { pState->m_Switch.FinishPending(); });
		std::thread Second;
		try
		{
			{
				std::unique_lock<std::mutex> Lock(pGate->m_Mutex);
				pGate->m_EnteredInTime = pGate->m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return pGate->m_Entered; });
			}
			Second = std::thread([pState, pGate] {
				{
					const std::lock_guard<std::mutex> Lock(pGate->m_Mutex);
					pGate->m_SecondStarted = true;
					pGate->m_Cv.notify_all();
				}
				pState->m_Switch.FinishPending();
				{
					const std::lock_guard<std::mutex> Lock(pGate->m_Mutex);
					pGate->m_SecondFinished = true;
					pGate->m_Cv.notify_all();
				}
			});
			{
				std::unique_lock<std::mutex> Lock(pGate->m_Mutex);
				pGate->m_StartedInTime = pGate->m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return pGate->m_SecondStarted; });
				pGate->m_ReturnedEarly = pGate->m_Cv.wait_for(Lock, std::chrono::milliseconds(20), [&] { return pGate->m_SecondFinished; });
				pGate->m_Release = true;
			}
			pGate->m_Cv.notify_all();
		}
		catch(...)
		{
			{
				const std::lock_guard<std::mutex> Lock(pGate->m_Mutex);
				pGate->m_Release = true;
			}
			pGate->m_Cv.notify_all();
			First.join();
			if(Second.joinable())
				Second.join();
			throw;
		}
		First.join();
		Second.join();
	}));
	EXPECT_TRUE(pGate->m_EnteredInTime);
	EXPECT_TRUE(pGate->m_StartedInTime);
	EXPECT_FALSE(pGate->m_ReturnedEarly);
	EXPECT_FALSE(pGate->m_TimedOut);
	EXPECT_EQ(pState->m_pOld->m_Destroyed.load(), 1);
}
