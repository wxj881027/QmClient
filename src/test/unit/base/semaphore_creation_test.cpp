#include <base/sphore.h>

#include <gtest/gtest.h>
#include <test/support/async_io_wait.h>

#include <memory>
#include <new>

TEST(SemaphoreCreation, FailedNativeInitializationReturnsNoObject)
{
	int Calls = 0;
	const auto pSemaphore = CSemaphore::TryCreate([&](SEMAPHORE *) {
		++Calls;
		return false;
	});
	EXPECT_EQ(pSemaphore, nullptr);
	EXPECT_EQ(Calls, 1);
}

TEST(SemaphoreCreation, InitializationAllocationExceptionReturnsNoObject)
{
	EXPECT_EQ(CSemaphore::TryCreate([](SEMAPHORE *) -> bool { throw std::bad_alloc(); }), nullptr);
}

TEST(SemaphoreCreation, UnknownInitializationExceptionReturnsNoObject)
{
	EXPECT_EQ(CSemaphore::TryCreate([](SEMAPHORE *) -> bool { throw 7; }), nullptr);
}

TEST(SemaphoreCreation, NativeCreationStartsWithNoAvailableSignals)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		const auto pSemaphore = CSemaphore::TryCreate();
		ASSERT_NE(pSemaphore, nullptr);
		EXPECT_EQ(pSemaphore->GetApproximateValue(), 0);
	}));
}

TEST(SemaphoreCreation, CreatedSemaphoreRetainsAndConsumesBothSignals)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		const auto pSemaphore = CSemaphore::TryCreate();
		ASSERT_NE(pSemaphore, nullptr);
		pSemaphore->Signal();
		pSemaphore->Signal();
		EXPECT_EQ(pSemaphore->GetApproximateValue(), 2);
		pSemaphore->Wait();
		EXPECT_EQ(pSemaphore->GetApproximateValue(), 1);
		pSemaphore->Wait();
		EXPECT_EQ(pSemaphore->GetApproximateValue(), 0);
	}));
}

TEST(SemaphoreCreation, FailedInitializationCanBeFollowedByAnOwnedNativeSemaphore)
{
	ASSERT_TRUE(WaitForAsyncIo([] {
		EXPECT_EQ(CSemaphore::TryCreate([](SEMAPHORE *) { return false; }), nullptr);
		int Calls = 0;
		const auto pSemaphore = CSemaphore::TryCreate([&](SEMAPHORE *pNative) {
			++Calls;
			return sphore_try_init(pNative);
		});
		ASSERT_NE(pSemaphore, nullptr);
		EXPECT_EQ(Calls, 1);
		pSemaphore->Signal();
		pSemaphore->Wait();
		EXPECT_EQ(pSemaphore->GetApproximateValue(), 0);
	}));
}
