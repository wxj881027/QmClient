#include <base/thread_startup.h>

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <new>

TEST(ThreadStartup, ReadinessIsPublishedBeforeTheBusinessEntry)
{
	CThreadStartup Startup;
	auto Result = Startup.GetResult();
	int Entries = 0;
	Startup.Run(true, [&] {
		EXPECT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
		++Entries;
	});
	ASSERT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_TRUE(CThreadStartup::Wait(Result));
	EXPECT_EQ(Entries, 1);
}

TEST(ThreadStartup, FailedInitializationPublishesFailureWithoutEnteringTheCallback)
{
	CThreadStartup Startup;
	auto Result = Startup.GetResult();
	int Entries = 0;
	Startup.Run(false, [&] { ++Entries; });
	ASSERT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_FALSE(CThreadStartup::Wait(Result));
	EXPECT_EQ(Entries, 0);
}

TEST(ThreadStartup, EarlyProducerDestructionReportsFailure)
{
	std::future<bool> Result;
	{
		CThreadStartup Startup;
		Result = Startup.GetResult();
	}
	ASSERT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_FALSE(CThreadStartup::Wait(Result));
}

TEST(ThreadStartup, AllocationExceptionInTheEntryDoesNotEscapeTheThreadBoundary)
{
	CThreadStartup Startup;
	auto Result = Startup.GetResult();
	int Entries = 0;
	Startup.Run(true, [&] {
		++Entries;
		throw std::bad_alloc();
	});
	ASSERT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_TRUE(CThreadStartup::Wait(Result));
	EXPECT_EQ(Entries, 1);
}

TEST(ThreadStartup, UnknownEntryExceptionDoesNotChangeTheSuccessfulStartupResult)
{
	CThreadStartup Startup;
	auto Result = Startup.GetResult();
	Startup.Run(true, [] { throw 7; });
	ASSERT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_TRUE(CThreadStartup::Wait(Result));
}

TEST(ThreadStartup, RepeatedPublicationDoesNotRunTheBusinessEntryTwice)
{
	CThreadStartup Startup;
	auto Result = Startup.GetResult();
	int Entries = 0;
	Startup.Run(true, [&] { ++Entries; });
	Startup.Run(true, [&] { ++Entries; });
	ASSERT_EQ(Result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_TRUE(CThreadStartup::Wait(Result));
	EXPECT_EQ(Entries, 1);
}
