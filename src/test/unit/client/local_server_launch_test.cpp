#include <game/client/components/local_server_launch.h>

#include <gtest/gtest.h>

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
