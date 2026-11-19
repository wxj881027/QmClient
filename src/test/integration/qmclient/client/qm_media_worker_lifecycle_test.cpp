#include <game/client/components/qmclient/media_volume_logic.h>
#include <game/client/components/qmclient/media_worker.h>
#include <game/client/components/system_media_controls_mpris_policy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

namespace
{
	struct CTrace
	{
		std::mutex m_Mutex;
		std::condition_variable m_Changed;
		std::atomic_int m_Runs{0}, m_Released{0}, m_Destroyed{0}, m_Failures{0}, m_Restarts{0}, m_Stopped{0};
		std::atomic_uint64_t m_Generation{0};
		std::atomic_bool m_Ready{false}, m_FailureAfterUnwind{true}, m_DestroyedAfterRelease{true};
		int m_FailRuns = 0;
		bool m_ReturnImmediately = false;
		void Notify()
		{
			const std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Changed.notify_all();
		}
		template<typename F>
		bool Wait(F Predicate)
		{
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			std::unique_lock<std::mutex> Lock(m_Mutex);
			while(!Predicate())
			{
				const auto Now = std::chrono::steady_clock::now();
				if(Now >= Deadline)
					return false;
				m_Changed.wait_until(Lock, std::min(Deadline, Now + std::chrono::milliseconds(5)));
			}
			return true;
		}
	};
	class CState : public CQmMediaWorker::CState
	{
		CTrace &m_Trace;
		SystemMediaControls::CMprisSessionIdentity m_Identity;

	public:
		explicit CState(CTrace &Trace) :
			m_Trace(Trace) {}
		~CState() override
		{
			m_Trace.m_DestroyedAfterRelease = m_Trace.m_Released >= m_Trace.m_Runs;
			++m_Trace.m_Destroyed;
			m_Trace.Notify();
		}
		void Run(const std::atomic_bool &Stop) override
		{
			const int Run = ++m_Trace.m_Runs;
			// 外部媒体资源只在工作回调期间存在，观察其先于失败回调回收。
			struct CResource
			{
				CTrace &m_Trace;
				~CResource()
				{
					++m_Trace.m_Released;
					m_Trace.Notify();
				}
			} Resource{m_Trace};
			m_Identity.Update("player", ":1.1");
			m_Trace.m_Generation = m_Identity.Generation();
			m_Trace.m_Ready = true;
			m_Trace.Notify();
			if(Run <= m_Trace.m_FailRuns)
				throw std::bad_alloc();
			if(m_Trace.m_ReturnImmediately)
				return;
			const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			std::unique_lock<std::mutex> Lock(m_Trace.m_Mutex);
			while(!Stop.load())
			{
				const auto Now = std::chrono::steady_clock::now();
				if(Now >= Deadline)
					throw std::runtime_error("media worker stop timeout");
				m_Trace.m_Changed.wait_until(Lock, std::min(Deadline, Now + std::chrono::milliseconds(5)));
			}
			++m_Trace.m_Stopped;
		}
		void OnFailure() noexcept override
		{
			m_Trace.m_Ready = false;
			m_Identity.Clear();
			m_Trace.m_FailureAfterUnwind = m_Trace.m_Released >= m_Trace.m_Runs;
			++m_Trace.m_Failures;
			m_Trace.Notify();
		}
		void BeforeRestart() noexcept override
		{
			m_Trace.m_Ready = false;
			m_Identity.Clear();
			++m_Trace.m_Restarts;
		}
	};
	class COperations : public CQmMediaWorker::COperations
	{
		CTrace &m_Trace;

	public:
		int m_CreateCalls = 0;
		bool m_FailThread = false;
		explicit COperations(CTrace &Trace) :
			m_Trace(Trace) {}
		std::unique_ptr<CQmMediaWorker::CState> CreateState() override
		{
			++m_CreateCalls;
			return std::make_unique<CState>(m_Trace);
		}
		std::thread CreateThread(CQmMediaWorker::CState &State, const std::atomic_bool &Stop, std::atomic_bool &Finished) override
		{
			if(m_FailThread)
				throw std::bad_alloc();
			return CQmMediaWorker::COperations::CreateThread(State, Stop, Finished);
		}
	};
}

TEST(QmMediaWorkerLifecycleIntegration, RunningStateIsPublishedOnceAndRepeatedEnsureDoesNotRecreateIt)
{
	CTrace Trace;
	COperations Operations(Trace);
	CQmMediaWorker Worker;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Ready.load(); }));
	const auto *pState = Worker.State();
	EXPECT_NE(pState, nullptr);
	EXPECT_TRUE(Worker.EnsureStarted(Operations, 1, 200));
	EXPECT_EQ(Worker.State(), pState);
	EXPECT_EQ(Operations.m_CreateCalls, 1);
	Worker.Stop();
	EXPECT_EQ(Trace.m_Destroyed, 1);
}

TEST(QmMediaWorkerLifecycleIntegration, NormalExitKeepsStateOwnedUntilTheMainThreadStopsIt)
{
	CTrace Trace;
	Trace.m_ReturnImmediately = true;
	COperations Operations(Trace);
	CQmMediaWorker Worker;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Runs == 1 && !Worker.Running(); }));
	EXPECT_NE(Worker.State(), nullptr);
	EXPECT_EQ(Trace.m_Destroyed, 0);
	Worker.Stop();
	EXPECT_EQ(Trace.m_Destroyed, 1);
	EXPECT_TRUE(Trace.m_DestroyedAfterRelease);
}

TEST(QmMediaWorkerLifecycleIntegration, FailureInvalidatesAfterUnwindAndRestartsTheSameOwnedState)
{
	CTrace Trace;
	Trace.m_FailRuns = 1;
	COperations Operations(Trace);
	CQmMediaWorker Worker;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Failures == 1 && !Worker.Running(); }));
	const auto *pState = Worker.State();
	EXPECT_FALSE(Trace.m_Ready);
	EXPECT_TRUE(Trace.m_FailureAfterUnwind);
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 199, 200));
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 200, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Runs == 2 && Trace.m_Ready; }));
	EXPECT_EQ(Worker.State(), pState);
	EXPECT_EQ(Operations.m_CreateCalls, 1);
	EXPECT_EQ(Trace.m_Restarts, 1);
	Worker.Stop();
}

TEST(QmMediaWorkerLifecycleIntegration, StopWaitsForWorkToReleaseResourcesBeforeDestroyingState)
{
	CTrace Trace;
	COperations Operations(Trace);
	CQmMediaWorker Worker;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Ready.load(); }));
	Worker.Stop();
	Worker.Stop();
	EXPECT_FALSE(Worker.Running());
	EXPECT_EQ(Worker.State(), nullptr);
	EXPECT_EQ(Trace.m_Stopped, 1);
	EXPECT_EQ(Trace.m_Released, 1);
	EXPECT_EQ(Trace.m_Destroyed, 1);
	EXPECT_TRUE(Trace.m_DestroyedAfterRelease);
}

TEST(QmMediaWorkerLifecycleIntegration, RestartKeepsSourceGenerationAndRejectsAnOldVolumeRequest)
{
	CTrace Trace;
	Trace.m_FailRuns = 1;
	COperations Operations(Trace);
	CQmMediaWorker Worker;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Failures == 1 && !Worker.Running(); }));
	const uint64_t Previous = Trace.m_Generation;
	QmMediaVolume::CPendingVolume Pending;
	Pending.Set(Previous, 0.5f);
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 200, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Runs == 2 && Trace.m_Ready; }));
	const uint64_t Current = Trace.m_Generation;
	EXPECT_GT(Current, Previous);
	EXPECT_FALSE(Pending.Take(Current));
	Pending.Set(Current, 0.25f);
	ASSERT_TRUE(Pending.Take(Current));
	Worker.Stop();
}

TEST(QmMediaWorkerLifecycleIntegration, FailedRelaunchKeepsStateForTheNextBoundedRetry)
{
	CTrace Trace;
	Trace.m_FailRuns = 1;
	COperations Operations(Trace);
	CQmMediaWorker Worker;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Failures == 1 && !Worker.Running(); }));
	const auto *pState = Worker.State();
	Operations.m_FailThread = true;
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 200, 200));
	EXPECT_EQ(Worker.State(), pState);
	EXPECT_FALSE(Worker.Running());
	EXPECT_EQ(Trace.m_Destroyed, 0);
	EXPECT_FALSE(Worker.EnsureStarted(Operations, 399, 200));
	Operations.m_FailThread = false;
	ASSERT_TRUE(Worker.EnsureStarted(Operations, 400, 200));
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Runs == 2 && Trace.m_Ready; }));
	EXPECT_EQ(Worker.State(), pState);
	EXPECT_EQ(Operations.m_CreateCalls, 1);
	Worker.Stop();
}

TEST(QmMediaWorkerLifecycleIntegration, DestructionStopsAndJoinsWithoutAnExplicitStopCall)
{
	CTrace Trace;
	COperations Operations(Trace);
	{
		CQmMediaWorker Worker;
		ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
		ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Ready.load(); }));
	}
	EXPECT_EQ(Trace.m_Stopped, 1);
	EXPECT_EQ(Trace.m_Destroyed, 1);
	EXPECT_TRUE(Trace.m_DestroyedAfterRelease);
}

TEST(QmMediaWorkerLifecycleIntegration, StartupOperationsMayEndAfterTheThreadOwnsItsState)
{
	CTrace Trace;
	CQmMediaWorker Worker;
	{
		COperations Operations(Trace);
		ASSERT_TRUE(Worker.EnsureStarted(Operations, 0, 200));
	}
	ASSERT_TRUE(Trace.Wait([&] { return Trace.m_Ready.load(); }));
	Worker.Stop();
	EXPECT_EQ(Trace.m_Destroyed, 1);
}
