#include <engine/client/client.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <memory>
#include <new>
#include <utility>

namespace
{
	struct SConcatLaunchFailure
	{
		SConcatLaunchFailure() = default;
		SConcatLaunchFailure(SConcatLaunchFailure &&) { throw std::bad_alloc(); }
		bool operator()() const { return true; }
	};

	struct SConcatResult
	{
		bool m_Pending = true;
		bool m_Failed = false;
		bool m_RetryRan = false;
		bool m_RetryFailed = true;
	};
}

TEST(VideoExportConcatWorker, LaunchFailureFinishesAndAllowsRetry)
{
	auto pResult = std::make_shared<SConcatResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CSegmentConcatWorker Worker;
		Worker.Start(SConcatLaunchFailure{});
		pResult->m_Pending = Worker.Pending();
		pResult->m_Failed = Worker.Failed();

		Worker.Start([pResult] {
			pResult->m_RetryRan = true;
			return true;
		});
		Worker.Join();
		pResult->m_RetryFailed = Worker.Failed();
	}));
	EXPECT_FALSE(pResult->m_Pending);
	EXPECT_TRUE(pResult->m_Failed);
	EXPECT_TRUE(pResult->m_RetryRan);
	EXPECT_FALSE(pResult->m_RetryFailed);
}

TEST(VideoExportConcatWorker, ExceptionFromWorkFinishesAndReportsFailure)
{
	auto pResult = std::make_shared<SConcatResult>();
	ASSERT_TRUE(WaitForAsyncIo([pResult] {
		CSegmentConcatWorker Worker;
		Worker.Start([]() -> bool {
			throw std::bad_alloc();
		});
		Worker.Join();
		pResult->m_Pending = Worker.Pending();
		pResult->m_Failed = Worker.Failed();
	}));
	EXPECT_FALSE(pResult->m_Pending);
	EXPECT_TRUE(pResult->m_Failed);
}
