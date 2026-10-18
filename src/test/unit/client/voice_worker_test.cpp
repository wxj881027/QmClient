#include <game/client/components/qmclient/voice/voice_worker.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace
{
	using namespace std::chrono_literals;

	template<typename TCondition>
	bool WaitUntil(TCondition Condition)
	{
		const auto Deadline = std::chrono::steady_clock::now() + 2s;
		while(!Condition() && std::chrono::steady_clock::now() < Deadline)
			std::this_thread::sleep_for(1ms);
		return Condition();
	}

	bool WaitForStop(CVoiceWorker &Worker)
	{
		return WaitUntil([&] { return Worker.StopRequested(); });
	}

	struct SObservedResource
	{
		std::atomic<bool> &m_Released;
		explicit SObservedResource(std::atomic<bool> &Released) :
			m_Released(Released) {}
		~SObservedResource() { m_Released = true; }
	};
}

TEST(VoiceWorker, StartFailureIsReportedBeforeRetryAndDoesNotRunTheBody)
{
	CVoiceWorker Worker;
	int Calls = 0;
	EXPECT_FALSE(Worker.Start(100, [&] { ++Calls; }, [](auto) -> std::thread { throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again)); }));
	EXPECT_TRUE(Worker.HasFailed());
	EXPECT_FALSE(Worker.HasThread());
	EXPECT_FALSE(Worker.Start(101, [&] { ++Calls; }));
	EXPECT_EQ(Calls, 0);
	bool Cleaned = false;
	EXPECT_TRUE(Worker.HandleFailure(101, 1000, [&](CVoiceWorker::EFailure Failure, const char *pError) {
		EXPECT_EQ(Failure, CVoiceWorker::EFailure::START);
		EXPECT_NE(pError[0], '\0');
		EXPECT_FALSE(Worker.HasThread());
		Cleaned = true;
	}));
	EXPECT_TRUE(Cleaned);
	EXPECT_FALSE(Worker.HasFailed());
	EXPECT_FALSE(Worker.CanStart(1100));
	EXPECT_TRUE(Worker.CanStart(1101));
}

TEST(VoiceWorker, AllocationFailureDuringThreadCreationIsContained)
{
	CVoiceWorker Worker;
	EXPECT_FALSE(Worker.Start(0, [] {}, [](auto) -> std::thread { throw std::bad_alloc(); }));
	EXPECT_TRUE(Worker.HandleFailure(0, 1000, [](CVoiceWorker::EFailure Failure, const char *pError) {
		EXPECT_EQ(Failure, CVoiceWorker::EFailure::START);
		EXPECT_NE(pError[0], '\0');
	}));
}

TEST(VoiceWorker, UnknownThreadCreationExceptionIsContained)
{
	CVoiceWorker Worker;
	EXPECT_FALSE(Worker.Start(0, [] {}, [](auto) -> std::thread { throw 1; }));
	EXPECT_TRUE(Worker.HandleFailure(0, 1000, [](CVoiceWorker::EFailure Failure, const char *pError) {
		EXPECT_EQ(Failure, CVoiceWorker::EFailure::START);
		EXPECT_STREQ(pError, "Unknown exception");
	}));
}

TEST(VoiceWorker, WorkerFailureUnwindsAndJoinsBeforeOwnerCleanup)
{
	std::atomic<bool> Released{false};
	CVoiceWorker Worker;
	ASSERT_TRUE(Worker.Start(0, [&] {
		SObservedResource Resource(Released);
		throw std::bad_alloc();
	}));
	ASSERT_TRUE(WaitUntil([&] { return Worker.HasFailed(); }));
	EXPECT_TRUE(Worker.HasThread());
	int Cleanups = 0;
	EXPECT_TRUE(Worker.HandleFailure(10, 1000, [&](CVoiceWorker::EFailure Failure, const char *pError) {
		EXPECT_EQ(Failure, CVoiceWorker::EFailure::RUN);
		EXPECT_NE(pError[0], '\0');
		EXPECT_TRUE(Released.load());
		EXPECT_FALSE(Worker.HasThread());
		// owner 的 Shutdown 可以重复停止已经回收的线程。
		Worker.Stop();
		++Cleanups;
	}));
	EXPECT_FALSE(Worker.HandleFailure(20, 1000, [&](CVoiceWorker::EFailure, const char *) { ++Cleanups; }));
	EXPECT_EQ(Cleanups, 1);
}

TEST(VoiceWorker, WorkerExceptionMessageSurvivesUntilTheOwnerHandlesIt)
{
	CVoiceWorker Worker;
	ASSERT_TRUE(Worker.Start(0, [] { throw std::runtime_error("voice transport allocation"); }));
	ASSERT_TRUE(WaitUntil([&] { return Worker.HasFailed(); }));
	EXPECT_TRUE(Worker.HandleFailure(0, 1000, [](CVoiceWorker::EFailure Failure, const char *pError) {
		EXPECT_EQ(Failure, CVoiceWorker::EFailure::RUN);
		EXPECT_STREQ(pError, "voice transport allocation");
	}));
}

TEST(VoiceWorker, UnknownWorkerExceptionIsReportedWithoutLeavingTheThread)
{
	CVoiceWorker Worker;
	ASSERT_TRUE(Worker.Start(0, [] { throw 1; }));
	ASSERT_TRUE(WaitUntil([&] { return Worker.HasFailed(); }));
	EXPECT_TRUE(Worker.HandleFailure(0, 1000, [](CVoiceWorker::EFailure Failure, const char *pError) {
		EXPECT_EQ(Failure, CVoiceWorker::EFailure::RUN);
		EXPECT_STREQ(pError, "Unknown exception");
	}));
	EXPECT_FALSE(Worker.HasThread());
}

TEST(VoiceWorker, StopDoesNotDiscardAnUnhandledFailure)
{
	CVoiceWorker Worker;
	ASSERT_TRUE(Worker.Start(0, [] { throw std::bad_alloc(); }));
	ASSERT_TRUE(WaitUntil([&] { return Worker.HasFailed(); }));
	Worker.Stop();
	Worker.Stop();
	EXPECT_FALSE(Worker.HasThread());
	EXPECT_TRUE(Worker.HasFailed());
	EXPECT_FALSE(Worker.Start(500, [] {}));
	EXPECT_TRUE(Worker.HandleFailure(500, 1000, [](CVoiceWorker::EFailure, const char *) {}));
}

TEST(VoiceWorker, RetryWaitsForCleanupAndDeadlineThenStartsANewWorker)
{
	std::atomic<int> Runs{0};
	std::atomic<bool> Stopped{false};
	CVoiceWorker Worker;
	ASSERT_TRUE(Worker.Start(0, [&] {
		++Runs;
		throw std::bad_alloc();
	}));
	ASSERT_TRUE(WaitUntil([&] { return Worker.HasFailed(); }));
	ASSERT_TRUE(Worker.HandleFailure(20, 1000, [](CVoiceWorker::EFailure, const char *) {}));
	const auto Run = [&] {
		++Runs;
		Stopped = WaitForStop(Worker);
	};
	EXPECT_FALSE(Worker.Start(1019, Run));
	EXPECT_EQ(Runs.load(), 1);
	ASSERT_TRUE(Worker.Start(1020, Run));
	ASSERT_TRUE(WaitUntil([&] { return Runs.load() == 2; }));
	Worker.Stop();
	EXPECT_TRUE(Stopped.load());
	EXPECT_FALSE(Worker.HasFailed());
}

TEST(VoiceWorker, CleanupCannotStartAReplacementBeforeReleasingOldResources)
{
	CVoiceWorker Worker;
	ASSERT_FALSE(Worker.Start(0, [] {}, [](auto) -> std::thread { throw std::bad_alloc(); }));
	EXPECT_TRUE(Worker.HandleFailure(10, 1000, [&](CVoiceWorker::EFailure, const char *) {
		EXPECT_FALSE(Worker.Start(10000, [] { ADD_FAILURE() << "cleanup is still in progress"; }));
	}));
	EXPECT_FALSE(Worker.HasThread());
	EXPECT_TRUE(Worker.CanStart(1010));
}

TEST(VoiceWorker, RepeatedStartDoesNotReplaceARunningThread)
{
	std::atomic<int> Runs{0};
	std::atomic<bool> Stopped{false};
	CVoiceWorker Worker;
	const auto Run = [&] {
		++Runs;
		Stopped = WaitForStop(Worker);
	};
	ASSERT_TRUE(Worker.Start(0, Run));
	ASSERT_TRUE(WaitUntil([&] { return Runs.load() == 1; }));
	EXPECT_TRUE(Worker.Start(1, Run));
	Worker.Stop();
	EXPECT_EQ(Runs.load(), 1);
	EXPECT_TRUE(Stopped.load());
	EXPECT_FALSE(Worker.HasThread());
	EXPECT_FALSE(Worker.HandleFailure(2, 1000, [](CVoiceWorker::EFailure, const char *) { ADD_FAILURE(); }));
}

TEST(VoiceWorker, NormalStopAllowsImmediateRestart)
{
	std::atomic<int> Runs{0};
	std::atomic<int> Stops{0};
	CVoiceWorker Worker;
	const auto Run = [&] {
		++Runs;
		if(WaitForStop(Worker))
			++Stops;
	};
	ASSERT_TRUE(Worker.Start(0, Run));
	ASSERT_TRUE(WaitUntil([&] { return Runs.load() == 1; }));
	Worker.Stop();
	ASSERT_TRUE(Worker.Start(0, Run));
	ASSERT_TRUE(WaitUntil([&] { return Runs.load() == 2; }));
	Worker.Stop();
	EXPECT_EQ(Stops.load(), 2);
}

TEST(VoiceWorker, DestructionRequestsStopAndJoinsBeforeReturning)
{
	std::atomic<bool> Running{false};
	std::atomic<bool> Stopped{false};
	{
		CVoiceWorker Worker;
		ASSERT_TRUE(Worker.Start(0, [&] {
			Running = true;
			Stopped = WaitForStop(Worker);
		}));
		ASSERT_TRUE(WaitUntil([&] { return Running.load(); }));
	}
	EXPECT_TRUE(Stopped.load());
}
