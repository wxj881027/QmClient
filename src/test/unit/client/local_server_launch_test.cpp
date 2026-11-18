#include <game/client/components/local_server_launch.h>

#include <gtest/gtest.h>

#include <cerrno>
#include <limits>
#include <new>

namespace
{
	using EBindingResult = CLocalServerLaunch::EBindingResult;
	class CLaunchResource : public IAllocationResource
	{
	public:
		int m_Calls = 0, m_Live = 0, m_FailAt = 0;
		void *Allocate(size_t Bytes, size_t Alignment) override
		{
			if(++m_Calls == m_FailAt)
				throw std::bad_alloc();
			auto *pData = AllocateDefault(Bytes, Alignment);
			++m_Live;
			return pData;
		}
		void Deallocate(void *pData, size_t Bytes, size_t Alignment) noexcept override
		{
			--m_Live;
			DeallocateDefault(pData, Bytes, Alignment);
		}
	};
}

TEST(LocalServerLaunch, PreparedRequestOwnsArgumentsAndAuthCommand)
{
	CLocalServerLaunch Launch;
	char aArgument[] = "sv_name 中文";
	char aAuth[] = "auth_add local admin password";
	const char *apArguments[] = {aArgument, ""};
	ASSERT_TRUE(Launch.TryPrepare(apArguments, 2, aAuth, true));
	aArgument[0] = 'x';
	aAuth[0] = 'x';
	ASSERT_EQ(Launch.Count(), 3u);
	EXPECT_STREQ(Launch.Data()[0], "sv_name 中文");
	EXPECT_STREQ(Launch.Data()[1], "");
	EXPECT_STREQ(Launch.Data()[2], "auth_add local admin password");
	EXPECT_TRUE(Launch.IsReady());
	EXPECT_TRUE(Launch.ConnectWhenReady());
}

TEST(LocalServerLaunch, EmptyArgumentListStillOwnsAuthCommand)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", false));
	ASSERT_EQ(Launch.Count(), 1u);
	EXPECT_STREQ(Launch.Data()[0], "auth");
}

TEST(LocalServerLaunch, NullInputsDoNotPublishPartialRequest)
{
	CLocalServerLaunch Launch;
	EXPECT_FALSE(Launch.TryPrepare(nullptr, 1, "auth", true));
	EXPECT_FALSE(Launch.TryPrepare(nullptr, 0, nullptr, true));
	const char *apArguments[] = {"valid", nullptr};
	EXPECT_FALSE(Launch.TryPrepare(apArguments, 2, "auth", true));
	EXPECT_FALSE(Launch.IsStarting());
	EXPECT_EQ(Launch.Count(), 0u);
	EXPECT_FALSE(Launch.ConnectWhenReady());
}

TEST(LocalServerLaunch, ImpossibleCountIsRejectedBeforeReadingArguments)
{
	CLocalServerLaunch Launch;
	const char *apArguments[] = {"valid"};
	EXPECT_FALSE(Launch.TryPrepare(apArguments, std::numeric_limits<size_t>::max(), "auth", false));
	EXPECT_FALSE(Launch.IsStarting());
}

TEST(LocalServerLaunch, EveryOwnedStringAndVectorAllocationFailureRollsBack)
{
	const std::string Argument(200, 'a');
	const std::string Auth(200, 'p');
	const char *apArguments[] = {Argument.c_str()};
	int Allocations = 0;
	CLaunchResource Baseline;
	{
		CLocalServerLaunch Launch(&Baseline);
		ASSERT_TRUE(Launch.TryPrepare(apArguments, 1, Auth.c_str(), true));
		Allocations = Baseline.m_Calls;
	}
	ASSERT_GE(Allocations, 4);
	ASSERT_EQ(Baseline.m_Live, 0);
	for(int FailAt = 1; FailAt <= Allocations; ++FailAt)
	{
		SCOPED_TRACE(FailAt);
		CLaunchResource Resource;
		Resource.m_FailAt = FailAt;
		{
			CLocalServerLaunch Launch(&Resource);
			EXPECT_FALSE(Launch.TryPrepare(apArguments, 1, Auth.c_str(), true));
			EXPECT_FALSE(Launch.IsStarting());
			EXPECT_EQ(Launch.Count(), 0u);
			EXPECT_FALSE(Launch.ConnectWhenReady());
			EXPECT_EQ(Resource.m_Live, 0);
		}
		EXPECT_EQ(Resource.m_Live, 0);
	}
}

TEST(LocalServerLaunch, AllocationFailureCanRetryOnSameRequestOwner)
{
	CLaunchResource Resource;
	Resource.m_FailAt = 1;
	CLocalServerLaunch Launch(&Resource);
	EXPECT_FALSE(Launch.TryPrepare(nullptr, 0, "auth", false));
	Resource.m_FailAt = 0;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "retry", false));
	EXPECT_STREQ(Launch.Data()[0], "retry");
}

TEST(LocalServerLaunch, RepeatedPreparationCannotReplacePendingAuth)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "first auth", true));
	const char *pFirst = Launch.Data()[0];
	EXPECT_FALSE(Launch.TryPrepare(nullptr, 0, "second auth", false));
	EXPECT_EQ(Launch.Data()[0], pFirst);
	EXPECT_STREQ(Launch.Data()[0], "first auth");
	EXPECT_TRUE(Launch.ConnectWhenReady());
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_FALSE(Launch.TryPrepare(nullptr, 0, "third auth", false));
	EXPECT_EQ(Launch.Data()[0], pFirst);
}

TEST(LocalServerLaunch, BindingKeepsPreparedPointersAndDoesNotExtendDeadline)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", false));
	const char *pAuth = Launch.Data()[0];
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_FALSE(Launch.MarkBinding(1000));
	EXPECT_EQ(Launch.Data()[0], pAuth);
	EXPECT_TRUE(Launch.IsStarting());
	EXPECT_TRUE(Launch.IsBinding());
	EXPECT_TRUE(Launch.BindingTimedOut(100 + CLocalServerLaunch::BIND_TIMEOUT_NANOS));
}

TEST(LocalServerLaunch, IdleOrReadyRequestDoesNotExpireAsBinding)
{
	CLocalServerLaunch Launch;
	EXPECT_FALSE(Launch.MarkBinding(10));
	EXPECT_EQ(Launch.PollBinding(true, 20), EBindingResult::WAITING);
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", false));
	EXPECT_FALSE(Launch.BindingTimedOut(std::numeric_limits<int64_t>::max()));
	EXPECT_EQ(Launch.PollBinding(true, std::numeric_limits<int64_t>::max()), EBindingResult::WAITING);
	EXPECT_TRUE(Launch.IsReady());
}

TEST(LocalServerLaunch, BoundServiceEmitsAutoConnectOnlyOnce)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", true));
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_EQ(Launch.PollBinding(false, 101), EBindingResult::WAITING);
	EXPECT_EQ(Launch.PollBinding(true, 102), EBindingResult::CONNECT);
	EXPECT_FALSE(Launch.IsStarting());
	EXPECT_EQ(Launch.Count(), 0u);
	EXPECT_EQ(Launch.PollBinding(true, 103), EBindingResult::WAITING);
}

TEST(LocalServerLaunch, BoundServiceWithoutAutoConnectCompletesWithoutConnectAction)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", false));
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_EQ(Launch.PollBinding(true, 101), EBindingResult::CONNECTED);
	EXPECT_FALSE(Launch.IsStarting());
}

TEST(LocalServerLaunch, MissingBindingTimesOutOnceAtDeadline)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", true));
	ASSERT_TRUE(Launch.MarkBinding(100));
	const int64_t Deadline = 100 + CLocalServerLaunch::BIND_TIMEOUT_NANOS;
	EXPECT_EQ(Launch.PollBinding(false, Deadline - 1), EBindingResult::WAITING);
	EXPECT_EQ(Launch.PollBinding(false, Deadline), EBindingResult::FAILED);
	EXPECT_FALSE(Launch.IsStarting());
	EXPECT_EQ(Launch.Count(), 0u);
	EXPECT_EQ(Launch.PollBinding(false, Deadline + 1), EBindingResult::WAITING);
}

TEST(LocalServerLaunch, ReadyConnectionAtDeadlineWinsOverTimeout)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", true));
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_EQ(Launch.PollBinding(true, 100 + CLocalServerLaunch::BIND_TIMEOUT_NANOS), EBindingResult::CONNECT);
}

TEST(LocalServerLaunch, ClockRegressionDoesNotImmediatelyFailBinding)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "auth", false));
	ASSERT_TRUE(Launch.MarkBinding(100));
	EXPECT_EQ(Launch.PollBinding(false, 99), EBindingResult::WAITING);
}

TEST(LocalServerLaunch, CancelledBindingCannotEmitLateConnectAndCanPrepareNewAuth)
{
	CLocalServerLaunch Launch;
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "old auth", true));
	ASSERT_TRUE(Launch.MarkBinding(100));
	Launch.Reset();
	Launch.Reset();
	EXPECT_EQ(Launch.PollBinding(true, 101), EBindingResult::WAITING);
	ASSERT_TRUE(Launch.TryPrepare(nullptr, 0, "new auth", false));
	EXPECT_STREQ(Launch.Data()[0], "new auth");
	EXPECT_TRUE(Launch.IsReady());
}

namespace
{
	class LocalServerUnixProcess : public testing::Test
	{
	protected:
		using TPid = CLocalServerUnixProcess::TPid;
		CLocalServerUnixProcess m_Process;
		CLocalServerUnixProcess::SProbeResult m_ProbeResult{};
		int m_StartCalls = 0;
		int m_SignalCalls = 0;
		int m_SignalError = 0;
		TPid m_LastProbed = 0;
		TPid m_LastSignalled = 0;

		bool Start(TPid Pid)
		{
			return m_Process.TryStart([&] {
				++m_StartCalls;
				return Pid;
			});
		}

		CLocalServerUnixProcess::SPollResult Poll()
		{
			return m_Process.Poll(
				[this](TPid Pid) {
					m_LastProbed = Pid;
					return m_ProbeResult;
				},
				[this](TPid Pid) {
					++m_SignalCalls;
					m_LastSignalled = Pid;
					return m_SignalError;
				});
		}
	};
}

TEST_F(LocalServerUnixProcess, DelayedExitBlocksRestartAndRepeatedStopSignals)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	EXPECT_FALSE(Poll().m_Released);

	for(int i = 0; i < 64; ++i)
	{
		m_Process.RequestStop();
		EXPECT_FALSE(Poll().m_Released);
	}
	EXPECT_EQ(m_SignalCalls, 1);
	EXPECT_EQ(m_LastProbed, 42);
	EXPECT_EQ(m_LastSignalled, 42);
	EXPECT_TRUE(m_Process.IsStopping());
	EXPECT_EQ(m_Process.Pid(), 42);
	EXPECT_FALSE(Start(43));
	EXPECT_EQ(m_StartCalls, 1);

	m_ProbeResult.m_Exited = true;
	ASSERT_TRUE(Poll().m_Released);
	EXPECT_FALSE(Poll().m_Released);
	ASSERT_TRUE(Start(43));
	EXPECT_EQ(m_StartCalls, 2);
	EXPECT_EQ(m_Process.Pid(), 43);
	EXPECT_FALSE(m_Process.IsStopping());
}

TEST_F(LocalServerUnixProcess, InterruptedProbeDefersStopToTheNextPoll)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	m_ProbeResult.m_Error = EINTR;
	const auto Interrupted = Poll();
	EXPECT_FALSE(Interrupted.m_Released);
	EXPECT_EQ(Interrupted.m_ProbeError, 0);
	EXPECT_EQ(m_SignalCalls, 0);
	EXPECT_EQ(m_Process.Pid(), 42);
	EXPECT_TRUE(m_Process.IsStopping());

	m_ProbeResult = {};
	EXPECT_FALSE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 1);
	EXPECT_EQ(m_LastSignalled, 42);
}

TEST_F(LocalServerUnixProcess, ExitedChildIsReapedWithoutSendingSignal)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	m_ProbeResult.m_Exited = true;
	EXPECT_TRUE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 0);
	EXPECT_FALSE(m_Process.HasProcess());
}

TEST_F(LocalServerUnixProcess, NoChildReleasesOwnershipWithoutSendingSignal)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	m_ProbeResult.m_Error = ECHILD;
	EXPECT_TRUE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 0);
	EXPECT_FALSE(m_Process.HasProcess());

	EXPECT_TRUE(Start(43));
}

TEST_F(LocalServerUnixProcess, MissingSignalTargetStillRequiresReapBeforeRestart)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	m_SignalError = ESRCH;
	const auto TargetGone = Poll();
	EXPECT_FALSE(TargetGone.m_Released);
	EXPECT_EQ(TargetGone.m_SignalError, 0);
	EXPECT_TRUE(m_Process.IsStopping());
	EXPECT_FALSE(Start(43));

	m_Process.RequestStop();
	EXPECT_FALSE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 1);
	m_ProbeResult.m_Error = ECHILD;
	EXPECT_TRUE(Poll().m_Released);
	EXPECT_TRUE(Start(43));
}

TEST_F(LocalServerUnixProcess, FailedSignalWaitsForAnExplicitRetry)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	m_SignalError = EPERM;
	EXPECT_EQ(Poll().m_SignalError, EPERM);
	EXPECT_TRUE(m_Process.IsStopping());
	EXPECT_FALSE(Start(43));
	m_SignalError = 0;
	EXPECT_FALSE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 1);

	m_Process.RequestStop();
	EXPECT_FALSE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 2);
	EXPECT_EQ(m_LastSignalled, 42);
}

TEST_F(LocalServerUnixProcess, UnexpectedProbeFailureRetainsOwnershipAndPollsAgain)
{
	ASSERT_TRUE(Start(42));
	m_Process.RequestStop();
	m_ProbeResult.m_Error = EINVAL;
	EXPECT_EQ(Poll().m_ProbeError, EINVAL);
	EXPECT_EQ(Poll().m_ProbeError, 0);
	EXPECT_EQ(m_SignalCalls, 0);
	EXPECT_FALSE(Start(43));
	EXPECT_EQ(m_Process.Pid(), 42);

	m_ProbeResult = {};
	EXPECT_FALSE(Poll().m_Released);
	EXPECT_EQ(m_SignalCalls, 1);
	EXPECT_EQ(m_LastSignalled, 42);
}
