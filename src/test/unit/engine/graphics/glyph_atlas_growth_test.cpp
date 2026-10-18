#include <engine/client/glyph_atlas_growth.h>

#include <gtest/gtest.h>

#include <array>

TEST(GlyphAtlasGrowth, PreservesBothTexturesAndClearsExpandedArea)
{
	std::array<uint8_t, 4> aFill{1, 2, 3, 4};
	std::array<uint8_t, 4> aOutline{5, 6, 7, 8};
	uint8_t *apOld[] = {aFill.data(), aOutline.data()};
	const auto apNew = PrepareGlyphAtlasGrowth(apOld, 2, 4);
	ASSERT_NE(apNew[0], nullptr);
	ASSERT_NE(apNew[1], nullptr);
	const std::array<uint8_t, 16> aExpectedFill{1, 2, 0, 0, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	const std::array<uint8_t, 16> aExpectedOutline{5, 6, 0, 0, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	for(size_t Index = 0; Index < aExpectedFill.size(); ++Index)
	{
		EXPECT_EQ(apNew[0][Index], aExpectedFill[Index]) << Index;
		EXPECT_EQ(apNew[1][Index], aExpectedOutline[Index]) << Index;
	}
	EXPECT_EQ(aFill, (std::array<uint8_t, 4>{1, 2, 3, 4}));
	EXPECT_EQ(aOutline, (std::array<uint8_t, 4>{5, 6, 7, 8}));
}

TEST(GlyphAtlasGrowth, FailedSecondAllocationDoesNotPublishPartialGrowth)
{
	uint8_t aFill[] = {1};
	uint8_t aOutline[] = {2};
	uint8_t *apOld[] = {aFill, aOutline};
	int Allocations = 0;
	const auto apNew = PrepareGlyphAtlasGrowth(apOld, 1, 2, [&](size_t Size) {
		return ++Allocations == 2 ? std::unique_ptr<uint8_t[]>() : std::make_unique<uint8_t[]>(Size);
	});
	EXPECT_EQ(Allocations, 2);
	EXPECT_EQ(apNew[0], nullptr);
	EXPECT_EQ(apNew[1], nullptr);
	EXPECT_EQ(aFill[0], 1);
	EXPECT_EQ(aOutline[0], 2);
}

TEST(GlyphAtlasGrowth, AllocationFailureCanRetryUsingUnchangedOldAtlas)
{
	uint8_t aFill[] = {3};
	uint8_t *apOld[] = {aFill};
	const auto apFailed = PrepareGlyphAtlasGrowth(apOld, 1, 2, [](size_t) { return std::unique_ptr<uint8_t[]>(); });
	EXPECT_EQ(apFailed[0], nullptr);
	const auto apRetry = PrepareGlyphAtlasGrowth(apOld, 1, 2);
	ASSERT_NE(apRetry[0], nullptr);
	EXPECT_EQ(apRetry[0][0], 3);
	EXPECT_EQ(apRetry[0][3], 0);
}

TEST(GlyphAtlasGrowth, InvalidDimensionsAreRejectedBeforeAllocation)
{
	uint8_t aFill[] = {1};
	uint8_t *apOld[] = {aFill};
	const auto Allocate = [](size_t) {
		ADD_FAILURE();
		return std::unique_ptr<uint8_t[]>();
	};
	EXPECT_EQ(PrepareGlyphAtlasGrowth(apOld, 0, 2, Allocate)[0], nullptr);
	EXPECT_EQ(PrepareGlyphAtlasGrowth(apOld, 1, 1, Allocate)[0], nullptr);
	EXPECT_EQ(PrepareGlyphAtlasGrowth(apOld, 2, 1, Allocate)[0], nullptr);
	EXPECT_EQ(PrepareGlyphAtlasGrowth(apOld, 1, std::numeric_limits<size_t>::max(), Allocate)[0], nullptr);
}

TEST(GlyphAtlasGrowth, MissingSourceTextureDoesNotAllocateOrReadPartialData)
{
	uint8_t aFill[] = {1};
	uint8_t *apOld[] = {aFill, nullptr};
	int Allocations = 0;
	const auto apNew = PrepareGlyphAtlasGrowth(apOld, 1, 2, [&](size_t) {
		++Allocations;
		return std::unique_ptr<uint8_t[]>();
	});
	EXPECT_EQ(Allocations, 0);
	EXPECT_EQ(apNew[0], nullptr);
	EXPECT_EQ(apNew[1], nullptr);
}
