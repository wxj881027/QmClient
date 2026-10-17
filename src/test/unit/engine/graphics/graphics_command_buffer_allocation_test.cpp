#include <engine/client/graphics_threaded.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

TEST(GraphicsCommandBufferAllocation, OversizedRequestCannotWrapIntoSmallAllocation)
{
	CCommandBuffer Buffer(128, 64);
	EXPECT_EQ(Buffer.AllocData(std::numeric_limits<size_t>::max()), nullptr);
	if constexpr(sizeof(size_t) > sizeof(unsigned))
	{
		const size_t TruncatedToSmall = static_cast<size_t>(std::numeric_limits<unsigned>::max()) + size_t{2};
		EXPECT_EQ(Buffer.AllocData(TruncatedToSmall), nullptr);
	}
	EXPECT_NE(Buffer.AllocData(64), nullptr);
}

TEST(GraphicsCommandBufferAllocation, FailedAllocationLeavesRemainingCapacityAvailable)
{
	constexpr size_t Alignment = alignof(std::max_align_t);
	CCommandBuffer Buffer(128, 4 * Alignment);
	ASSERT_NE(Buffer.AllocData(Alignment), nullptr);
	EXPECT_EQ(Buffer.AllocData(std::numeric_limits<size_t>::max()), nullptr);
	EXPECT_NE(Buffer.AllocData(3 * Alignment), nullptr);
	EXPECT_EQ(Buffer.AllocData(1), nullptr);
}

TEST(GraphicsCommandBufferAllocation, AlignmentPaddingCountsTowardCapacity)
{
	CCommandBuffer Buffer(128, 2 * alignof(std::max_align_t));
	ASSERT_NE(Buffer.AllocData(1), nullptr);
	void *pAligned = Buffer.AllocData(alignof(std::max_align_t));
	ASSERT_NE(pAligned, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(pAligned) % alignof(std::max_align_t), 0u);
	EXPECT_EQ(Buffer.AllocData(1), nullptr);
}

TEST(GraphicsCommandBufferAllocation, ResetMakesFullCapacityAvailableAgain)
{
	CCommandBuffer Buffer(128, 64);
	void *pFirst = Buffer.AllocData(64);
	ASSERT_NE(pFirst, nullptr);
	Buffer.Reset();
	EXPECT_EQ(Buffer.AllocData(64), pFirst);
}
