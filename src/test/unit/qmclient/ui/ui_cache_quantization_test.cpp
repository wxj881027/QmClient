#include <game/client/QmUi/UiCacheQuantization.h>

#include <gtest/gtest.h>

TEST(UiCacheQuantization, FontTenthsKeepSignedHalfAwayFromZeroRounding)
{
	EXPECT_EQ(UiCacheRoundedBucket(12.0f, 10.0f), 120);
	EXPECT_EQ(UiCacheRoundedBucket(1.25f, 10.0f), 13);
	EXPECT_EQ(UiCacheRoundedBucket(-1.25f, 10.0f), -13);
	EXPECT_EQ(UiCacheRoundedBucket(0.0f, 10.0f), 0);
}

TEST(UiCacheQuantization, KeepsTheLegacyFloatIntermediateRounding)
{
	EXPECT_EQ(UiCacheRoundedBucket(16777215.0f, 1.0f), 16777216);
	EXPECT_EQ(UiCacheRoundedBucket(-16777215.0f, 1.0f), -16777216);
}

TEST(UiCacheQuantization, FiniteOverflowSaturatesBeforeTheIntegerCast)
{
	const float Large = std::numeric_limits<float>::max();
	EXPECT_EQ(UiCacheRoundedBucket(Large, 100.0f), std::numeric_limits<int>::max());
	EXPECT_EQ(UiCacheRoundedBucket(-Large, 100.0f), std::numeric_limits<int>::min());
	EXPECT_EQ(UiCacheRoundedBucket(static_cast<float>(std::numeric_limits<int>::max()), 1.0f), std::numeric_limits<int>::max());
	EXPECT_EQ(UiCacheRoundedBucket(static_cast<float>(std::numeric_limits<int>::min()), 1.0f), std::numeric_limits<int>::min());
}

TEST(UiCacheQuantization, NonfiniteOperandsUseTheRequestedInvalidBucket)
{
	const float Inf = std::numeric_limits<float>::infinity();
	EXPECT_EQ(UiCacheRoundedBucket(std::numeric_limits<float>::quiet_NaN(), 10.0f), 0);
	EXPECT_EQ(UiCacheRoundedBucket(Inf, 10.0f), 0);
	EXPECT_EQ(UiCacheRoundedBucket(-Inf, 10.0f), 0);
	EXPECT_EQ(UiCacheRoundedBucket(1.0f, Inf, 47), 47);
}

TEST(UiCacheQuantization, WidthBucketsKeepFourUnitAlignmentAtTheIntegerBoundary)
{
	EXPECT_EQ(UiTextWidthBucket(0.0f), 0);
	EXPECT_EQ(UiTextWidthBucket(1.0f), 0);
	EXPECT_EQ(UiTextWidthBucket(2.0f), 4);
	EXPECT_EQ(UiTextWidthBucket(6.0f), 8);
	EXPECT_EQ(UiTextWidthBucket(10.0f), 12);
	const int LargestAligned = std::numeric_limits<int>::max() / 4 * 4;
	EXPECT_EQ(UiTextWidthBucket(static_cast<float>(std::numeric_limits<int>::max())), LargestAligned);
	EXPECT_EQ(UiTextWidthBucket(std::numeric_limits<float>::max()), LargestAligned);
}

TEST(UiCacheQuantization, InvalidWidthsKeepTheUnboundedSentinel)
{
	EXPECT_EQ(UiTextWidthBucket(-1.0f), -1);
	EXPECT_EQ(UiTextWidthBucket(std::numeric_limits<float>::quiet_NaN()), -1);
	EXPECT_EQ(UiTextWidthBucket(std::numeric_limits<float>::infinity()), -1);
	EXPECT_EQ(UiTextWidthBucket(-std::numeric_limits<float>::infinity()), -1);
}

TEST(UiCacheQuantization, HiDpiHundredthsKeepNormalRounding)
{
	EXPECT_EQ(UiTextHiDpiBucket(0.0f), 0);
	EXPECT_EQ(UiTextHiDpiBucket(1.0f), 100);
	EXPECT_EQ(UiTextHiDpiBucket(1.25f), 125);
	EXPECT_EQ(UiTextHiDpiBucket(1.125f), 113);
}

TEST(UiCacheQuantization, InvalidHiDpiKeepsDefaultAndFiniteOverflowUsesTheTypeLimit)
{
	EXPECT_EQ(UiTextHiDpiBucket(-1.0f), 100);
	EXPECT_EQ(UiTextHiDpiBucket(std::numeric_limits<float>::quiet_NaN()), 100);
	EXPECT_EQ(UiTextHiDpiBucket(std::numeric_limits<float>::infinity()), 100);
	EXPECT_EQ(UiTextHiDpiBucket(std::numeric_limits<float>::max()), std::numeric_limits<int>::max());
}
