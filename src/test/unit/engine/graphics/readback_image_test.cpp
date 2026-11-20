#include <engine/client/backend/readback_image.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace
{
	struct SAllocationState
	{
		bool m_Fail = false;
		int m_Calls = 0;
	};

	template<typename T>
	struct CFailingAllocator
	{
		using value_type = T;
		SAllocationState *m_pState;
		explicit CFailingAllocator(SAllocationState *pState) :
			m_pState(pState) {}
		template<typename U>
		CFailingAllocator(const CFailingAllocator<U> &Other) :
			m_pState(Other.m_pState)
		{
		}
		T *allocate(size_t Count)
		{
			++m_pState->m_Calls;
			if(m_pState->m_Fail)
				throw std::bad_alloc();
			return std::allocator<T>{}.allocate(Count);
		}
		void deallocate(T *pData, size_t Count) { std::allocator<T>{}.deallocate(pData, Count); }
		template<typename U>
		bool operator==(const CFailingAllocator<U> &Other) const
		{
			return m_pState == Other.m_pState;
		}
	};
	using TBuffer = std::vector<uint8_t, CFailingAllocator<uint8_t>>;
}

TEST(ReadbackImage, PackedRgbaPreservesPixelsAndAlpha)
{
	const std::array<uint8_t, 8> aSource{1, 2, 3, 4, 5, 6, 7, 8};
	std::vector<uint8_t> vOutput;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 2, 1, 8, aSource.size(), false, false));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(ReadbackImage, PaddedRowsCopyOnlyPixelBytesIncludingTheUnpaddedLastRow)
{
	const std::array<uint8_t, 20> aSource{1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 99, 99, 9, 10, 11, 12, 13, 14, 15, 16};
	std::vector<uint8_t> vOutput;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 2, 2, 12, aSource.size(), false, false));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}));
}

TEST(ReadbackImage, OneRowDoesNotRequireTrailingPitchPadding)
{
	const std::array<uint8_t, 4> aSource{1, 2, 3, 4};
	std::vector<uint8_t> vOutput;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 1, 1, 1024, aSource.size(), false, false));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{1, 2, 3, 4}));
}

TEST(ReadbackImage, BgraFallbackSwizzlesChannelsAndKeepsItsOpaqueAlphaPolicy)
{
	const std::array<uint8_t, 8> aSource{3, 2, 1, 0, 7, 6, 5, 127};
	std::vector<uint8_t> vOutput;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 2, 1, 8, aSource.size(), true, false));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{1, 2, 3, 255, 5, 6, 7, 255}));
}

TEST(ReadbackImage, ResetAlphaDoesNotSwapRgbaChannels)
{
	const std::array<uint8_t, 8> aSource{1, 2, 3, 0, 5, 6, 7, 127};
	std::vector<uint8_t> vOutput;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 1, 2, 4, aSource.size(), false, true));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{1, 2, 3, 255, 5, 6, 7, 255}));
}

TEST(ReadbackImage, EmptyDimensionsRejectBeforeAllocating)
{
	const std::array<uint8_t, 4> aSource{1, 2, 3, 4};
	SAllocationState State;
	TBuffer vOutput{CFailingAllocator<uint8_t>(&State)};
	EXPECT_FALSE(readback_image::PackRgba(vOutput, aSource.data(), 0, 1, 4, aSource.size(), false, false));
	EXPECT_FALSE(readback_image::PackRgba(vOutput, aSource.data(), 1, 0, 4, aSource.size(), false, false));
	EXPECT_EQ(State.m_Calls, 0);
	EXPECT_TRUE(vOutput.empty());
}

TEST(ReadbackImage, ShortPitchRejectsBeforeAllocating)
{
	const std::array<uint8_t, 8> aSource{};
	SAllocationState State;
	TBuffer vOutput{CFailingAllocator<uint8_t>(&State)};
	EXPECT_FALSE(readback_image::PackRgba(vOutput, aSource.data(), 2, 1, 7, aSource.size(), false, false));
	EXPECT_EQ(State.m_Calls, 0);
}

TEST(ReadbackImage, TruncatedLastPixelLeavesExistingOutputUnchanged)
{
	const std::array<uint8_t, 11> aSource{};
	std::vector<uint8_t> vOutput{42};
	EXPECT_FALSE(readback_image::PackRgba(vOutput, aSource.data(), 1, 2, 8, aSource.size(), false, false));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{42}));
}

TEST(ReadbackImage, NullMappingLeavesExistingOutputUnchanged)
{
	std::vector<uint8_t> vOutput{42};
	EXPECT_FALSE(readback_image::PackRgba(vOutput, nullptr, 1, 1, 4, 4, false, false));
	EXPECT_EQ(vOutput, (std::vector<uint8_t>{42}));
}

TEST(ReadbackImage, OverflowingPixelCountRejectsLayout)
{
	readback_image::SRgbaLayout Layout;
	EXPECT_FALSE(readback_image::GetRgbaLayout(std::numeric_limits<size_t>::max(), 2, 4, std::numeric_limits<uint64_t>::max(), Layout));
	EXPECT_EQ(Layout.m_DataBytes, 0U);
}

TEST(ReadbackImage, OverflowingPixelBytesRejectLayout)
{
	readback_image::SRgbaLayout Layout;
	EXPECT_FALSE(readback_image::GetRgbaLayout(std::numeric_limits<size_t>::max() / 4 + 1, 1, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), Layout));
	EXPECT_EQ(Layout.m_DataBytes, 0U);
}

TEST(ReadbackImage, UnaddressableRowStrideRejectsLayout)
{
	readback_image::SRgbaLayout Layout;
	EXPECT_FALSE(readback_image::GetRgbaLayout(1, 2, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), Layout));
	EXPECT_EQ(Layout.m_SourceBytes, 0U);
}

TEST(ReadbackImage, SourceSpanCannotOverflowTheAddressableRange)
{
	readback_image::SRgbaLayout Layout;
	const uint64_t MaxBytes = std::numeric_limits<ptrdiff_t>::max();
	EXPECT_FALSE(readback_image::GetRgbaLayout(1, 3, MaxBytes / 2, std::numeric_limits<uint64_t>::max(), Layout));
	EXPECT_EQ(Layout.m_SourceBytes, 0U);
}

TEST(ReadbackImage, LargeRowOffsetsAreNotTruncatedToThirtyTwoBits)
{
	if(sizeof(size_t) <= 4)
		GTEST_SKIP();
	readback_image::SRgbaLayout Layout;
	const uint64_t Pitch = uint64_t{1} << 32;
	ASSERT_TRUE(readback_image::GetRgbaLayout(1, 2, Pitch, Pitch + 4, Layout));
	EXPECT_EQ(Layout.m_RowPitch, Pitch);
	EXPECT_EQ(Layout.m_SourceBytes, Pitch + 4);
	EXPECT_EQ(Layout.m_DataBytes, 8U);
}

TEST(ReadbackImage, AllocationFailurePreservesExistingStorageAndPixels)
{
	const std::array<uint8_t, 8> aSource{};
	SAllocationState State;
	TBuffer vOutput({42}, CFailingAllocator<uint8_t>(&State));
	const uint8_t *pPrevious = vOutput.data();
	State.m_Fail = true;
	EXPECT_FALSE(readback_image::PackRgba(vOutput, aSource.data(), 2, 1, 8, aSource.size(), false, false));
	ASSERT_EQ(vOutput.size(), 1U);
	EXPECT_EQ(vOutput.data(), pPrevious);
	EXPECT_EQ(vOutput[0], 42);
}

TEST(ReadbackImage, ReadbackCanRetryAfterAllocationFailure)
{
	const std::array<uint8_t, 4> aSource{1, 2, 3, 4};
	SAllocationState State;
	TBuffer vOutput{CFailingAllocator<uint8_t>(&State)};
	State.m_Fail = true;
	EXPECT_FALSE(readback_image::PackRgba(vOutput, aSource.data(), 1, 1, 4, aSource.size(), false, false));
	State.m_Fail = false;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 1, 1, 4, aSource.size(), false, false));
	EXPECT_EQ((std::vector<uint8_t>(vOutput.begin(), vOutput.end())), (std::vector<uint8_t>{1, 2, 3, 4}));
}

TEST(ReadbackImage, ExistingLargeBufferNeedsNoFurtherAllocation)
{
	const std::array<uint8_t, 4> aSource{1, 2, 3, 4};
	SAllocationState State;
	TBuffer vOutput(8, 42, CFailingAllocator<uint8_t>(&State));
	const int Calls = State.m_Calls;
	State.m_Fail = true;
	ASSERT_TRUE(readback_image::PackRgba(vOutput, aSource.data(), 1, 1, 4, aSource.size(), false, false));
	EXPECT_EQ(State.m_Calls, Calls);
	EXPECT_EQ((std::vector<uint8_t>(vOutput.begin(), vOutput.end())), (std::vector<uint8_t>{1, 2, 3, 4, 42, 42, 42, 42}));
}
