// 原生 new 故障仅在预算操作期间启用；不把 malloc 或 codec scratch 计入故障注入证据。
#include <game/client/components/qmclient/settings_asset_memory.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <atomic>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	struct SAttempt
	{
		bool m_Success;
		bool m_Triggered;
		size_t m_Attempts;
	};
	template<typename FOperation>
	SAttempt Attempt(size_t BeforeFailure, FOperation &&Operation)
	{
		CPredictionAllocationFailure Failure(BeforeFailure);
		const bool Success = Operation();
		return {Success, Failure.Triggered(), Failure.Attempts()};
	}

	CImageInfo BorrowedImage(uint8_t *pData, size_t Width = 1, size_t Height = 1, CImageInfo::EImageFormat Format = CImageInfo::FORMAT_RGBA)
	{
		CImageInfo Image;
		Image.m_Width = Width;
		Image.m_Height = Height;
		Image.m_Format = Format;
		Image.m_pData = pData;
		return Image;
	}

	void ReleaseToken(SGraphicsUploadRelease &Token)
	{
		const auto Taken = std::exchange(Token, {});
		if(Taken.m_pfnRelease != nullptr)
			Taken.m_pfnRelease(Taken.m_pContext);
	}
}

TEST(SettingsAssetMemory, ExactLimitSucceedsAndNextBytePreservesExistingReservation)
{
	CSettingsAssetMemoryBudget Budget(10);
	CSettingsAssetMemoryLease First, Last, Rejected;
	ASSERT_TRUE(Budget.TryAcquire(6, First));
	ASSERT_TRUE(Budget.TryAcquire(4, Last));
	EXPECT_EQ(Budget.Used(), 10u);
	EXPECT_FALSE(Budget.TryAcquire(1, Rejected));
	EXPECT_FALSE(Rejected.HasReservation());
	EXPECT_EQ(First.Bytes(), 6u);
	EXPECT_EQ(Last.Bytes(), 4u);
	EXPECT_EQ(Budget.Used(), 10u);
	EXPECT_EQ(Budget.DeferredSequence(), 1u);
	Last.Reset();
	ASSERT_TRUE(Budget.TryAcquire(4, Rejected));
	EXPECT_EQ(Budget.Used(), Budget.Limit());
}

TEST(SettingsAssetMemory, ZeroReservationDoesNotAllocateAndReleasesReplacedOwner)
{
	CSettingsAssetMemoryBudget Previous(8), Zero(0);
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(Previous.TryAcquire(8, Lease));
	const auto Result = Attempt(0, [&] { return Zero.TryAcquire(0, Lease); });
	EXPECT_TRUE(Result.m_Success);
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_EQ(Result.m_Attempts, 0u);
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Previous.Used(), 0u);
	EXPECT_EQ(Zero.Used(), 0u);
}

TEST(SettingsAssetMemory, MaximumCounterCannotWrapIntoAvailableCapacity)
{
	const uint64_t Max = std::numeric_limits<uint64_t>::max();
	CSettingsAssetMemoryBudget Budget(Max);
	CSettingsAssetMemoryLease Most, Tail, Rejected;
	ASSERT_TRUE(Budget.TryAcquire(Max - 7, Most));
	ASSERT_TRUE(Budget.TryAcquire(7, Tail));
	EXPECT_EQ(Budget.Used(), Max);
	EXPECT_FALSE(Budget.TryAcquire(1, Rejected));
	EXPECT_EQ(Budget.Used(), Max);
	Most.Reset();
	EXPECT_EQ(Budget.Used(), 7u);
	Tail.Reset();
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST(SettingsAssetMemory, MoveAssignmentRetiresOnlyItsPreviousOwner)
{
	CSettingsAssetMemoryBudget Budget(32);
	CSettingsAssetMemoryLease First, Replaced;
	ASSERT_TRUE(Budget.TryAcquire(12, First));
	ASSERT_TRUE(Budget.TryAcquire(5, Replaced));
	CSettingsAssetMemoryLease Moved(std::move(First));
	EXPECT_FALSE(First.HasReservation());
	EXPECT_EQ(Moved.Bytes(), 12u);
	EXPECT_EQ(Budget.Used(), 17u);
	Replaced = std::move(Moved);
	EXPECT_FALSE(Moved.HasReservation());
	EXPECT_EQ(Replaced.Bytes(), 12u);
	EXPECT_EQ(Budget.Used(), 12u);
	Replaced.Reset();
	Replaced.Reset();
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST(SettingsAssetMemory, ShrinkCannotGrowAndReturnsOnlyReleasedBytes)
{
	CSettingsAssetMemoryBudget Budget(20);
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(Budget.TryAcquire(20, Lease));
	EXPECT_FALSE(Lease.Shrink(21));
	EXPECT_EQ(Lease.Bytes(), 20u);
	EXPECT_EQ(Budget.Used(), 20u);
	EXPECT_TRUE(Lease.Shrink(8));
	EXPECT_EQ(Lease.Bytes(), 8u);
	EXPECT_EQ(Budget.Used(), 8u);
	EXPECT_TRUE(Lease.Shrink(8));
	EXPECT_TRUE(Lease.Shrink(0));
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Budget.Used(), 0u);
	EXPECT_FALSE(Lease.Shrink(1));
}

TEST(SettingsAssetMemory, CloneNeedsIndependentCapacityAndFailureKeepsBothOwners)
{
	CSettingsAssetMemoryBudget Budget(16);
	CSettingsAssetMemoryLease Source, Destination;
	ASSERT_TRUE(Budget.TryAcquire(8, Source));
	ASSERT_TRUE(Budget.TryAcquire(4, Destination));
	EXPECT_FALSE(Source.TryClone(Destination));
	EXPECT_EQ(Source.Bytes(), 8u);
	EXPECT_EQ(Destination.Bytes(), 4u);
	EXPECT_EQ(Budget.Used(), 12u);
	Destination.Reset();
	ASSERT_TRUE(Source.TryClone(Destination));
	EXPECT_EQ(Budget.Used(), 16u);
	Source.Reset();
	EXPECT_EQ(Destination.Bytes(), 8u);
	EXPECT_EQ(Budget.Used(), 8u);
}

TEST(SettingsAssetMemory, DetachedTokenOutlivesBudgetAndReturnsOnceToNativeCallback)
{
	SGraphicsUploadRelease Token;
	{
		CSettingsAssetMemoryBudget Budget(8);
		CSettingsAssetMemoryLease Lease;
		ASSERT_TRUE(Budget.TryAcquire(8, Lease));
		Token = Lease.Detach();
		EXPECT_FALSE(Lease.HasReservation());
		EXPECT_NE(Token.m_pContext, nullptr);
		EXPECT_NE(Token.m_pfnRelease, nullptr);
		EXPECT_EQ(Budget.Used(), 8u);
	}
	ReleaseToken(Token);
	EXPECT_EQ(Token.m_pContext, nullptr);
	EXPECT_EQ(Token.m_pfnRelease, nullptr);
	ReleaseToken(Token);
}

TEST(SettingsAssetMemory, AdoptMoveShrinkAndNativeReleaseDoNotAllocate)
{
	CSettingsAssetMemoryBudget Budget(8);
	CSettingsAssetMemoryLease Source;
	ASSERT_TRUE(Budget.TryAcquire(8, Source));
	auto Token = Source.Detach();
	const auto Result = Attempt(0, [&] {
		auto Adopted = CSettingsAssetMemoryLease::Adopt(Token);
		CSettingsAssetMemoryLease Moved(std::move(Adopted));
		if(!Moved.HasReservation() || !Moved.Shrink(4))
			return false;
		auto Returned = Moved.Detach();
		ReleaseToken(Returned);
		return !Moved.HasReservation();
	});
	EXPECT_TRUE(Result.m_Success);
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_EQ(Result.m_Attempts, 0u);
	EXPECT_EQ(Token.m_pContext, nullptr);
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST(SettingsAssetMemory, ForeignTokenCannotBeAdoptedAsQuotaContext)
{
	int Calls = 0;
	SGraphicsUploadRelease Token{&Calls, [](void *pContext) noexcept { ++*static_cast<int *>(pContext); }};
	const auto Original = Token;
	auto Lease = CSettingsAssetMemoryLease::Adopt(Token);
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Token.m_pContext, Original.m_pContext);
	EXPECT_EQ(Token.m_pfnRelease, Original.m_pfnRelease);
	ReleaseToken(Token);
	EXPECT_EQ(Calls, 1);
}

TEST(SettingsAssetMemory, LazyStateAllocationFailureCanRetryWithoutTerminating)
{
	CSettingsAssetMemoryBudget Budget(8);
	CSettingsAssetMemoryLease Lease;
	const auto Failed = Attempt(0, [&] { return Budget.TryAcquire(8, Lease); });
	EXPECT_FALSE(Failed.m_Success);
	EXPECT_TRUE(Failed.m_Triggered);
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Budget.Used(), 0u);
	EXPECT_EQ(Budget.DeferredSequence(), 1u);
	ASSERT_TRUE(Budget.TryAcquire(8, Lease));
	EXPECT_EQ(Budget.Used(), 8u);
}

TEST(SettingsAssetMemory, ContextAllocationFailureRollsBackAndKeepsReplacedLease)
{
	CSettingsAssetMemoryBudget Budget(16);
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(Budget.TryAcquire(4, Lease));
	const auto Failed = Attempt(0, [&] { return Budget.TryAcquire(8, Lease); });
	EXPECT_FALSE(Failed.m_Success);
	EXPECT_TRUE(Failed.m_Triggered);
	EXPECT_EQ(Lease.Bytes(), 4u);
	EXPECT_EQ(Budget.Used(), 4u);
	EXPECT_EQ(Budget.DeferredSequence(), 1u);
	ASSERT_TRUE(Budget.TryAcquire(8, Lease));
	EXPECT_EQ(Lease.Bytes(), 8u);
	EXPECT_EQ(Budget.Used(), 8u);
}

TEST(SettingsAssetMemory, QuotaRejectionDoesNotAllocateReleaseContext)
{
	CSettingsAssetMemoryBudget Budget(8);
	CSettingsAssetMemoryLease Full, Rejected;
	ASSERT_TRUE(Budget.TryAcquire(8, Full));
	const auto Failed = Attempt(0, [&] { return Budget.TryAcquire(1, Rejected); });
	EXPECT_FALSE(Failed.m_Success);
	EXPECT_FALSE(Failed.m_Triggered);
	EXPECT_EQ(Failed.m_Attempts, 0u);
	EXPECT_FALSE(Rejected.HasReservation());
	EXPECT_EQ(Budget.Used(), 8u);
}

TEST(SettingsAssetMemory, CloneContextAllocationFailurePreservesIndependentOwners)
{
	CSettingsAssetMemoryBudget Budget(32);
	CSettingsAssetMemoryLease Source, Destination;
	ASSERT_TRUE(Budget.TryAcquire(8, Source));
	ASSERT_TRUE(Budget.TryAcquire(4, Destination));
	const auto Failed = Attempt(0, [&] { return Source.TryClone(Destination); });
	EXPECT_FALSE(Failed.m_Success);
	EXPECT_TRUE(Failed.m_Triggered);
	EXPECT_EQ(Source.Bytes(), 8u);
	EXPECT_EQ(Destination.Bytes(), 4u);
	EXPECT_EQ(Budget.Used(), 12u);
	ASSERT_TRUE(Source.TryClone(Destination));
	EXPECT_EQ(Budget.Used(), 16u);
}

TEST(SettingsAssetMemory, ConcurrentReservationsStayWithinOneSharedLimit)
{
	CSettingsAssetMemoryBudget Budget(24);
	std::atomic<bool> Invalid{false};
	std::atomic<unsigned> Acquired{0};
	std::vector<std::thread> Threads;
	Threads.reserve(8);
	try
	{
		for(int Index = 0; Index < 8; ++Index)
			Threads.emplace_back([&] {
				for(int Attempt = 0; Attempt < 128; ++Attempt)
				{
					CSettingsAssetMemoryLease Lease;
					if(Budget.TryAcquire(8, Lease))
					{
						++Acquired;
						if(Lease.Bytes() != 8 || Budget.Used() > Budget.Limit())
							Invalid = true;
						std::this_thread::yield();
					}
				}
			});
	}
	catch(...)
	{
		for(auto &Thread : Threads)
			Thread.join();
		throw;
	}
	for(auto &Thread : Threads)
		Thread.join();
	EXPECT_FALSE(Invalid.load());
	EXPECT_GT(Acquired.load(), 0u);
	EXPECT_EQ(Budget.Used(), 0u);
}

TEST(SettingsAssetMemory, WorkingBudgetCoversEncodedCopyResizeAndSourceFallbackReady)
{
	constexpr uint64_t MiB = 1024 * 1024;
	uint64_t Bytes = 0;
	ASSERT_TRUE(SettingsAssetMemoryComputeWorkingBytes(64 * MiB, 64 * MiB, 4096, Bytes));
	EXPECT_EQ(Bytes, 192 * MiB + 1);
	ASSERT_TRUE(SettingsAssetMemoryComputeWorkingBytes(0, 64 * MiB, 1, Bytes));
	EXPECT_EQ(Bytes, 128 * MiB);
	ASSERT_TRUE(SettingsAssetMemoryComputeWorkingBytes(0, 64 * MiB, 0, Bytes));
	EXPECT_EQ(Bytes, 128 * MiB);
	ASSERT_TRUE(SettingsAssetMemoryComputeWorkingBytes(0, 64 * MiB, std::numeric_limits<int>::max(), Bytes));
	EXPECT_EQ(Bytes, 128 * MiB + 1);
}

TEST(SettingsAssetMemory, WorkingBudgetClampsEncodedInterfaceWidthAndRejectsDecodedOverflow)
{
	uint64_t Bytes = 0;
	ASSERT_TRUE(SettingsAssetMemoryComputeWorkingBytes(std::numeric_limits<size_t>::max(), 1, 0, Bytes));
	EXPECT_EQ(Bytes, uint64_t(std::numeric_limits<unsigned>::max()) * 2 + 2);
	if(std::numeric_limits<size_t>::max() > std::numeric_limits<uint64_t>::max() / 2)
	{
		EXPECT_FALSE(SettingsAssetMemoryComputeWorkingBytes(0, std::numeric_limits<size_t>::max(), 0, Bytes));
		EXPECT_EQ(Bytes, 0u);
	}
}

TEST(SettingsAssetMemory, EveryValidPixelFormatRetainsTwoRgbaImages)
{
	std::array<uint8_t, 24> Pixels{};
	for(const auto Format : {CImageInfo::FORMAT_RGB, CImageInfo::FORMAT_RGBA, CImageInfo::FORMAT_R, CImageInfo::FORMAT_RA})
	{
		SCOPED_TRACE(Format);
		auto Image = BorrowedImage(Pixels.data(), 2, 3, Format);
		uint64_t Bytes = 0;
		ASSERT_TRUE(SettingsAssetMemoryReadyBytes(Image, Bytes));
		EXPECT_EQ(Bytes, 48u);
	}
}

TEST(SettingsAssetMemory, InvalidFormatCannotReachPixelSizeLookup)
{
	uint8_t Pixel = 0;
	for(const auto Format : {CImageInfo::FORMAT_UNDEFINED, static_cast<CImageInfo::EImageFormat>(-2), static_cast<CImageInfo::EImageFormat>(4)})
	{
		SCOPED_TRACE(Format);
		auto Image = BorrowedImage(&Pixel, 1, 1, Format);
		uint64_t Bytes = 99;
		EXPECT_FALSE(SettingsAssetMemoryReadyBytes(Image, Bytes));
		EXPECT_EQ(Bytes, 0u);
	}
}

TEST(SettingsAssetMemory, ZeroDimensionsCannotAcquireImageQuota)
{
	uint8_t Pixel = 0;
	for(const bool EmptyWidth : {false, true})
	{
		SCOPED_TRACE(EmptyWidth);
		auto Image = BorrowedImage(&Pixel, EmptyWidth ? 0 : 1, EmptyWidth ? 1 : 0);
		uint64_t Bytes = 99;
		EXPECT_FALSE(SettingsAssetMemoryReadyBytes(Image, Bytes));
		EXPECT_EQ(Bytes, 0u);
	}
}

TEST(SettingsAssetMemory, OverflowingReadyPixelSizeCannotWrapQuota)
{
	uint8_t Pixel = 0;
	uint64_t Bytes = 99;
	auto Huge = BorrowedImage(&Pixel, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
	EXPECT_FALSE(SettingsAssetMemoryReadyBytes(Huge, Bytes));
	EXPECT_EQ(Bytes, 0u);
}

TEST(SettingsAssetMemory, EmptyImageNeedsNoQuotaAndReleasesPreviousReservation)
{
	CSettingsAssetMemoryBudget Budget(8), Zero(0);
	CSettingsAssetMemoryLease Lease;
	ASSERT_TRUE(Budget.TryAcquire(8, Lease));
	CImageInfo Empty;
	const auto Result = Attempt(0, [&] { return SettingsAssetMemoryTryAcquireImage(Empty, Lease, &Zero); });
	EXPECT_TRUE(Result.m_Success);
	EXPECT_FALSE(Result.m_Triggered);
	EXPECT_EQ(Result.m_Attempts, 0u);
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Budget.Used(), 0u);
	EXPECT_EQ(Zero.Used(), 0u);
}

TEST(SettingsAssetMemory, ImageWithoutExistingLeaseUsesCommonImageAdmission)
{
	std::array<uint8_t, 16> Pixels{};
	auto Image = BorrowedImage(Pixels.data(), 2, 2);
	CSettingsAssetMemoryBudget Budget(16);
	CSettingsAssetMemoryLease Lease;
	EXPECT_FALSE(SettingsAssetMemoryTryAcquireImage(Image, Lease, &Budget));
	EXPECT_FALSE(Lease.HasReservation());
	EXPECT_EQ(Budget.DeferredSequence(), 1u);
	Image.m_Width = Image.m_Height = 1;
	ASSERT_TRUE(SettingsAssetMemoryTryAcquireImage(Image, Lease, &Budget));
	EXPECT_EQ(Lease.Bytes(), 8u);
	EXPECT_EQ(Budget.Used(), 8u);
}
