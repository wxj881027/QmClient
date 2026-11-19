#include <engine/storage.h>

#include <game/client/components/qmclient/music_lyrics/qm_soda_hook_provider.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>
#include <test/test.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
	constexpr uint64_t RETRY_WINDOW_MS = 5000;

	struct SStartObservation
	{
		size_t m_Allocations = 0;
		bool m_Accepted = false;
		bool m_Triggered = false;
		bool m_ResourceExceptionEscaped = false;
	};

	SStartObservation ObserveStart(CQmSodaHookProvider &Provider, const char *pPath, const char *pSource, size_t Budget)
	{
		SStartObservation Observation;
		CPredictionAllocationFailure Failure(Budget);
		try
		{
			Observation.m_Accepted = Provider.Start(pPath, pSource);
		}
		catch(const std::bad_alloc &)
		{
			Observation.m_ResourceExceptionEscaped = true;
		}
		catch(const std::length_error &)
		{
			Observation.m_ResourceExceptionEscaped = true;
		}
		Observation.m_Allocations = Failure.Attempts();
		Observation.m_Triggered = Failure.Triggered();
		return Observation;
	}

	std::string Status(const CQmSodaHookProvider &Provider)
	{
		char aStatus[256]{};
		Provider.GetStatus(aStatus, sizeof(aStatus));
		return aStatus;
	}

	void ExpectNoAllocation(const SStartObservation &Observation)
	{
		EXPECT_TRUE(Observation.m_Accepted);
		EXPECT_FALSE(Observation.m_Triggered);
		EXPECT_FALSE(Observation.m_ResourceExceptionEscaped);
		EXPECT_EQ(Observation.m_Allocations, 0u);
	}

	class SodaHookProviderAllocation : public ::testing::Test
	{
		CTestInfo m_Info;
		std::unique_ptr<IStorage> m_pStorage;

	protected:
		std::string m_MissingHelper;

		void SetUp() override
		{
			m_pStorage = m_Info.CreateTestStorage();
			ASSERT_NE(m_pStorage, nullptr);
			ASSERT_FALSE(m_pStorage->FileExists("absent-helper.exe", IStorage::TYPE_SAVE));
			char aPath[IO_MAX_PATH_LENGTH];
			m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "absent-helper.exe", aPath, sizeof(aPath));
			const auto Path = std::filesystem::absolute(std::filesystem::u8path(aPath)).u8string();
			m_MissingHelper.assign(reinterpret_cast<const char *>(Path.data()), Path.size());
		}
	};
}

TEST_F(SodaHookProviderAllocation, InitialPreparationRefusalKeepsImmediateRetryAndOrdinaryFailureKeepsCooldown)
{
	CQmSodaHookProvider TraceProvider;
	const auto Trace = ObserveStart(TraceProvider, m_MissingHelper.c_str(), "soda", std::numeric_limits<size_t>::max());
	ASSERT_TRUE(Trace.m_Accepted);
	ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 128u);
	ASSERT_FALSE(Status(TraceProvider).empty());
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmSodaHookProvider Provider;
		const uint64_t AttemptAt = GetTickCount64();
		const auto Refused = ObserveStart(Provider, m_MissingHelper.c_str(), "soda", Budget);
		EXPECT_FALSE(Refused.m_Accepted);
		EXPECT_TRUE(Refused.m_Triggered);
		EXPECT_FALSE(Refused.m_ResourceExceptionEscaped);
		EXPECT_TRUE(Status(Provider).empty());
		const uint64_t RetryAt = GetTickCount64();
		const auto Retried = ObserveStart(Provider, m_MissingHelper.c_str(), "soda", std::numeric_limits<size_t>::max());
		if(GetTickCount64() - AttemptAt >= RETRY_WINDOW_MS)
			GTEST_SKIP() << "主线程暂停已跨过启动节流窗口，不能观察即时重试";
		EXPECT_TRUE(Retried.m_Accepted);
		EXPECT_FALSE(Retried.m_Triggered);
		EXPECT_FALSE(Retried.m_ResourceExceptionEscaped);
		EXPECT_GT(Retried.m_Allocations, 0u);
		const std::string Error = Status(Provider);
		EXPECT_NE(Error.find("歌词采集器启动失败"), std::string::npos);
		const auto Throttled = ObserveStart(Provider, m_MissingHelper.c_str(), "soda", 0);
		if(GetTickCount64() - RetryAt >= RETRY_WINDOW_MS)
			GTEST_SKIP() << "主线程暂停已跨过启动节流窗口，不能观察普通失败后的冷却";
		ExpectNoAllocation(Throttled);
		EXPECT_EQ(Status(Provider), Error);
	}
}

TEST_F(SodaHookProviderAllocation, SourceSwitchPreparationRefusalPreservesTheOldSourceAndCooldownUntilRetry)
{
	CQmSodaHookProvider TraceProvider;
	ASSERT_TRUE(TraceProvider.Start(m_MissingHelper.c_str(), "soda"));
	const auto Trace = ObserveStart(TraceProvider, m_MissingHelper.c_str(), "qqmusic", std::numeric_limits<size_t>::max());
	ASSERT_TRUE(Trace.m_Accepted);
	ASSERT_FALSE(Trace.m_ResourceExceptionEscaped);
	ASSERT_FALSE(Trace.m_Triggered);
	ASSERT_GT(Trace.m_Allocations, 0u);
	ASSERT_LE(Trace.m_Allocations, 128u);
	for(size_t Budget = 0; Budget < Trace.m_Allocations; ++Budget)
	{
		SCOPED_TRACE(Budget);
		CQmSodaHookProvider Provider;
		const uint64_t BaselineAt = GetTickCount64();
		ASSERT_TRUE(Provider.Start(m_MissingHelper.c_str(), "soda"));
		const std::string Error = Status(Provider);
		ASSERT_FALSE(Error.empty());
		const auto Refused = ObserveStart(Provider, m_MissingHelper.c_str(), "qqmusic", Budget);
		EXPECT_FALSE(Refused.m_Accepted);
		EXPECT_TRUE(Refused.m_Triggered);
		EXPECT_FALSE(Refused.m_ResourceExceptionEscaped);
		EXPECT_EQ(Status(Provider), Error);
		const auto OldSource = ObserveStart(Provider, m_MissingHelper.c_str(), "soda", 0);
		if(GetTickCount64() - BaselineAt >= RETRY_WINDOW_MS)
			GTEST_SKIP() << "主线程暂停已跨过旧来源的启动节流窗口";
		ExpectNoAllocation(OldSource);
		const uint64_t RetryAt = GetTickCount64();
		const auto Retried = ObserveStart(Provider, m_MissingHelper.c_str(), "qqmusic", std::numeric_limits<size_t>::max());
		EXPECT_TRUE(Retried.m_Accepted);
		EXPECT_FALSE(Retried.m_Triggered);
		EXPECT_FALSE(Retried.m_ResourceExceptionEscaped);
		EXPECT_GT(Retried.m_Allocations, 0u);
		EXPECT_FALSE(Status(Provider).empty());
		const auto NewSource = ObserveStart(Provider, m_MissingHelper.c_str(), "qqmusic", 0);
		if(GetTickCount64() - RetryAt >= RETRY_WINDOW_MS)
			GTEST_SKIP() << "主线程暂停已跨过新来源的启动节流窗口";
		ExpectNoAllocation(NewSource);
	}
}

TEST_F(SodaHookProviderAllocation, SetupPreparationRefusalReturnsFalseAndPreservesTheExistingStatus)
{
	wchar_t aModule[MAX_PATH];
	const DWORD Length = GetModuleFileNameW(nullptr, aModule, MAX_PATH);
	if(Length == 0 || Length >= MAX_PATH)
		GTEST_SKIP() << "默认 helper 路径不可表示，生产入口会在分配前直接返回";
	CQmSodaHookProvider Provider;
	ASSERT_TRUE(Provider.Start(m_MissingHelper.c_str(), "soda"));
	const std::string Error = Status(Provider);
	ASSERT_FALSE(Error.empty());
	for(const bool Restore : {false, true})
	{
		SCOPED_TRACE(Restore);
		bool Started = true;
		bool Triggered = false;
		bool ResourceExceptionEscaped = false;
		{
			// 只拒收首次准备分配；任何真实安装/恢复进程都没有机会启动。
			CPredictionAllocationFailure Failure(0);
			try
			{
				Started = Provider.RunKugouSetup(Restore);
			}
			catch(const std::bad_alloc &)
			{
				ResourceExceptionEscaped = true;
			}
			catch(const std::length_error &)
			{
				ResourceExceptionEscaped = true;
			}
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(ResourceExceptionEscaped);
		EXPECT_FALSE(Started);
		EXPECT_EQ(Status(Provider), Error);
	}
}

#endif
