#include <game/client/components/qmclient/monitoring/monitoring.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace
{
	struct CProbe
	{
		std::mutex m_Mutex;
		std::condition_variable m_Cv;
		int m_Queries = 0;
		int m_Released = 0;
		int m_ThrowQuery = -1;
		int m_ThrowKind = 1;
		int m_SourceAttempts = 0;
		int m_SourceDestroyed = 0;
		int m_FailSourceAt = -1;
		int m_SourceFailure = 1;
		bool m_TimedOut = false;
		bool m_StopStarted = false;
		std::function<void()> m_OnDestroy;
		std::array<std::chrono::steady_clock::time_point, 8> m_aQueryTimes{};
		std::chrono::steady_clock::time_point m_FailureTime;

		SQmDevicePerfSample Query()
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			const int Call = ++m_Queries;
			if(Call <= (int)m_aQueryTimes.size())
				m_aQueryTimes[Call - 1] = std::chrono::steady_clock::now();
			m_Cv.notify_all();
			if(!m_Cv.wait_for(Lock, std::chrono::seconds(2), [&] { return m_Released >= Call; }))
				m_TimedOut = true;
			if(Call == m_ThrowQuery)
			{
				m_FailureTime = std::chrono::steady_clock::now();
				if(m_ThrowKind == 1)
					throw std::bad_alloc();
				throw 7;
			}
			SQmDevicePerfSample Result;
			Result.m_CpuUsagePct = (float)Call;
			Result.m_MemoryUsageMb = 100.0f + Call;
			Result.m_GpuUtilPct = (float)Call * 2.0f;
			Result.m_Available = true;
			return Result;
		}

		bool WaitForQuery(int Count)
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return m_Queries >= Count; });
		}
		bool WaitForDestroyed(int Count)
		{
			std::unique_lock<std::mutex> Lock(m_Mutex);
			return m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return m_SourceDestroyed >= Count; });
		}
		void Release(int Count)
		{
			{
				const std::lock_guard<std::mutex> Lock(m_Mutex);
				m_Released = Count;
			}
			m_Cv.notify_all();
		}
	};

	class CProbeSource : public CQmAsyncDevicePerfSampler::ISampleSource
	{
		std::shared_ptr<CProbe> m_pProbe;

	public:
		explicit CProbeSource(std::shared_ptr<CProbe> pProbe) :
			m_pProbe(std::move(pProbe)) {}
		~CProbeSource() override
		{
			{
				const std::lock_guard<std::mutex> Lock(m_pProbe->m_Mutex);
				++m_pProbe->m_SourceDestroyed;
				m_pProbe->m_Cv.notify_all();
			}
			if(m_pProbe->m_OnDestroy)
				m_pProbe->m_OnDestroy();
		}
		SQmDevicePerfSample Sample() override { return m_pProbe->Query(); }
	};

	class CProbeOperations : public CQmAsyncDevicePerfSampler::COperations
	{
		std::shared_ptr<CProbe> m_pProbe;

	public:
		std::atomic<int> m_ThreadAttempts{0};
		std::atomic<bool> m_FailThread{false};
		explicit CProbeOperations(std::shared_ptr<CProbe> pProbe) :
			m_pProbe(std::move(pProbe)) {}
		std::thread CreateThread(std::function<void()> Entry) override
		{
			++m_ThreadAttempts;
			if(m_FailThread)
				throw std::bad_alloc();
			return COperations::CreateThread(std::move(Entry));
		}
		std::unique_ptr<CQmAsyncDevicePerfSampler::ISampleSource> CreateSource() override
		{
			{
				const std::lock_guard<std::mutex> Lock(m_pProbe->m_Mutex);
				if(++m_pProbe->m_SourceAttempts == m_pProbe->m_FailSourceAt)
				{
					if(m_pProbe->m_SourceFailure == 1)
						throw std::bad_alloc();
					return nullptr;
				}
			}
			return std::make_unique<CProbeSource>(m_pProbe);
		}
	};

	struct CState
	{
		std::shared_ptr<CProbe> m_pProbe = std::make_shared<CProbe>();
		CProbeOperations m_Operations{m_pProbe};
		std::unique_ptr<CQmAsyncDevicePerfSampler> m_pSampler;

		explicit CState(bool ProcessCallback = false, std::chrono::milliseconds Interval = std::chrono::milliseconds(1))
		{
			CQmAsyncDevicePerfSampler::FSampleOverride Callback;
			if(ProcessCallback)
				Callback = [pProbe = m_pProbe] { return pProbe->Query(); };
			m_pSampler = std::make_unique<CQmAsyncDevicePerfSampler>(std::move(Callback), Interval, &m_Operations);
		}
		~CState()
		{
			m_pProbe->Release(1000000);
			m_pSampler.reset();
		}
	};

	template<typename F>
	::testing::AssertionResult RunSamplerCase(std::shared_ptr<CState> pState, F Function)
	{
		return WaitForAsyncIo([pState = std::move(pState), Function = std::move(Function)] {
			struct CStopGuard
			{
				CState &m_State;
				~CStopGuard()
				{
					m_State.m_pProbe->Release(1000000);
					m_State.m_pSampler->Stop();
					EXPECT_FALSE(m_State.m_pProbe->m_TimedOut);
				}
			} Guard{*pState};
			Function(*pState);
		});
	}

	void ExpectUnavailable(const SQmDevicePerfSnapshot &Snapshot, uint64_t Version)
	{
		EXPECT_EQ(Snapshot.m_Version, Version);
		EXPECT_FALSE(Snapshot.m_Sample.m_Available);
		EXPECT_FLOAT_EQ(Snapshot.m_Sample.m_CpuUsagePct, -1.0f);
		EXPECT_FLOAT_EQ(Snapshot.m_Sample.m_GpuUtilPct, -1.0f);
		EXPECT_FLOAT_EQ(Snapshot.m_Sample.m_MemoryUsageMb, -1.0f);
	}
}

TEST(QmDeviceSamplerRecovery, SourceAllocationFailurePublishesUnavailableThenRetries)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_FailSourceAt = 1;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 1);
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		const auto Recovered = State.m_pSampler->Snapshot();
		EXPECT_EQ(Recovered.m_Version, 2u);
		EXPECT_TRUE(Recovered.m_Sample.m_Available);
		EXPECT_FLOAT_EQ(Recovered.m_Sample.m_CpuUsagePct, 1.0f);
	}));
}

TEST(QmDeviceSamplerRecovery, EmptySourceCanRecoverOnTheNextPoll)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_FailSourceAt = 1;
	pState->m_pProbe->m_SourceFailure = 2;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 1);
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		EXPECT_EQ(State.m_pSampler->Snapshot().m_Version, 2u);
		EXPECT_TRUE(State.m_pSampler->Snapshot().m_Sample.m_Available);
	}));
}

TEST(QmDeviceSamplerRecovery, FailedQueryReleasesItsSourceBeforeRecreatingIt)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_ThrowQuery = 1;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 1);
		const std::lock_guard<std::mutex> Lock(State.m_pProbe->m_Mutex);
		EXPECT_EQ(State.m_pProbe->m_SourceAttempts, 2);
		EXPECT_EQ(State.m_pProbe->m_SourceDestroyed, 1);
	}));
}

TEST(QmDeviceSamplerRecovery, FailedQueryInvalidatesOldValuesAndRecoveryKeepsVersionIncreasing)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_ThrowQuery = 2;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		CQmDevicePerfVersionTracker Tracker;
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		EXPECT_TRUE(Tracker.Observe(State.m_pSampler->Snapshot().m_Version));
		State.m_pProbe->Release(2);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(3));
		const auto Failed = State.m_pSampler->Snapshot();
		ExpectUnavailable(Failed, 2);
		EXPECT_TRUE(Tracker.Observe(Failed.m_Version));
		State.m_pProbe->Release(3);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(4));
		const auto Recovered = State.m_pSampler->Snapshot();
		EXPECT_EQ(Recovered.m_Version, 3u);
		EXPECT_TRUE(Tracker.Observe(Recovered.m_Version));
		EXPECT_TRUE(Recovered.m_Sample.m_Available);
		EXPECT_FLOAT_EQ(Recovered.m_Sample.m_CpuUsagePct, 3.0f);
	}));
}

TEST(QmDeviceSamplerRecovery, UnknownQueryExceptionDoesNotTerminateTheWorker)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_ThrowQuery = 1;
	pState->m_pProbe->m_ThrowKind = 2;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 1);
		State.m_pProbe->Release(2);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(3));
		EXPECT_TRUE(State.m_pSampler->Snapshot().m_Sample.m_Available);
	}));
}

TEST(QmDeviceSamplerRecovery, ProcessCallbackFailureRecoversWithoutCreatingADeviceSource)
{
	auto pState = std::make_shared<CState>(true);
	pState->m_pProbe->m_ThrowQuery = 1;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 1);
		State.m_pProbe->Release(2);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(3));
		EXPECT_TRUE(State.m_pSampler->Snapshot().m_Sample.m_Available);
		const std::lock_guard<std::mutex> Lock(State.m_pProbe->m_Mutex);
		EXPECT_EQ(State.m_pProbe->m_SourceAttempts, 0);
	}));
}

TEST(QmDeviceSamplerRecovery, FailedStartupCanRetryThroughTheStateUpdater)
{
	auto pState = std::make_shared<CState>();
	pState->m_Operations.m_FailThread = true;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ExpectUnavailable(State.m_pSampler->Snapshot(), 0);
		EXPECT_EQ(State.m_Operations.m_ThreadAttempts.load(), 1);
		State.m_Operations.m_FailThread = false;
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		EXPECT_EQ(State.m_Operations.m_ThreadAttempts.load(), 2);
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		EXPECT_TRUE(State.m_pSampler->Snapshot().m_Sample.m_Available);
	}));
}

TEST(QmDeviceSamplerRecovery, DisableAndReenableRejectOldFailedResult)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_ThrowQuery = 1;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, false);
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 0);
		State.m_pProbe->Release(2);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(3));
		EXPECT_EQ(State.m_pSampler->Snapshot().m_Version, 1u);
		EXPECT_FLOAT_EQ(State.m_pSampler->Snapshot().m_Sample.m_CpuUsagePct, 2.0f);
	}));
}

TEST(QmDeviceSamplerRecovery, StopDuringFailedQueryWaitsForReleaseAndClearsSnapshot)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_ThrowQuery = 1;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		auto Stop = std::async(std::launch::async, [&State] {
			{
				const std::lock_guard<std::mutex> Lock(State.m_pProbe->m_Mutex);
				State.m_pProbe->m_StopStarted = true;
				State.m_pProbe->m_Cv.notify_all();
			}
			State.m_pSampler->Stop();
		});
		{
			std::unique_lock<std::mutex> Lock(State.m_pProbe->m_Mutex);
			EXPECT_TRUE(State.m_pProbe->m_Cv.wait_for(Lock, std::chrono::seconds(1), [&] { return State.m_pProbe->m_StopStarted; }));
		}
		EXPECT_EQ(Stop.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
		State.m_pProbe->Release(1);
		EXPECT_EQ(Stop.wait_for(std::chrono::seconds(1)), std::future_status::ready);
		Stop.get();
		ExpectUnavailable(State.m_pSampler->Snapshot(), 0);
	}));
}

TEST(QmDeviceSamplerRecovery, DisablingClosesTheSourceOutsideTheStateMutex)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_OnDestroy = [pSampler = pState->m_pSampler.get()] { pSampler->SetEnabled(false); };
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, false);
		State.m_pProbe->Release(1);
		EXPECT_TRUE(State.m_pProbe->WaitForDestroyed(1));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 0);
	}));
}

TEST(QmDeviceSamplerRecovery, FailedSourceCleanupCanPauseTheSamplerWithoutDeadlock)
{
	auto pState = std::make_shared<CState>();
	pState->m_pProbe->m_ThrowQuery = 1;
	pState->m_pProbe->m_OnDestroy = [pSampler = pState->m_pSampler.get()] { pSampler->SetEnabled(false); };
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		State.m_pProbe->Release(1);
		EXPECT_TRUE(State.m_pProbe->WaitForDestroyed(1));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 0);
	}));
}

TEST(QmDeviceSamplerRecovery, ReenableWhileOldQueryReturnsRebuildsTheDeviceSource)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, false);
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		ExpectUnavailable(State.m_pSampler->Snapshot(), 0);
		const std::lock_guard<std::mutex> Lock(State.m_pProbe->m_Mutex);
		EXPECT_EQ(State.m_pProbe->m_SourceAttempts, 2);
		EXPECT_EQ(State.m_pProbe->m_SourceDestroyed, 1);
	}));
}

TEST(QmDeviceSamplerRecovery, FailedQueryStillWaitsForTheConfiguredPollInterval)
{
	auto pState = std::make_shared<CState>(false, std::chrono::milliseconds(25));
	pState->m_pProbe->m_ThrowQuery = 1;
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		State.m_pProbe->Release(1);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(2));
		const std::lock_guard<std::mutex> Lock(State.m_pProbe->m_Mutex);
		EXPECT_GE(State.m_pProbe->m_aQueryTimes[1] - State.m_pProbe->m_FailureTime + std::chrono::milliseconds(1), std::chrono::milliseconds(25));
	}));
}

TEST(QmDeviceSamplerRecovery, RepeatedEnableReusesWorkerAndStopAllowsANewWorker)
{
	auto pState = std::make_shared<CState>();
	ASSERT_TRUE(RunSamplerCase(pState, [](CState &State) {
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(1));
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		EXPECT_EQ(State.m_Operations.m_ThreadAttempts.load(), 1);
		State.m_pProbe->Release(1000000);
		State.m_pSampler->Stop();
		int NextQuery;
		{
			const std::lock_guard<std::mutex> Lock(State.m_pProbe->m_Mutex);
			NextQuery = State.m_pProbe->m_Queries + 1;
		}
		State.m_pProbe->Release(NextQuery - 1);
		QmUpdateDevicePerfSamplerState(*State.m_pSampler, true);
		EXPECT_EQ(State.m_Operations.m_ThreadAttempts.load(), 2);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(NextQuery));
		State.m_pProbe->Release(NextQuery);
		ASSERT_TRUE(State.m_pProbe->WaitForQuery(NextQuery + 1));
		EXPECT_EQ(State.m_pSampler->Snapshot().m_Version, 1u);
		EXPECT_FLOAT_EQ(State.m_pSampler->Snapshot().m_Sample.m_CpuUsagePct, (float)NextQuery);
	}));
}
