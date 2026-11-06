#include <game/client/components/tclient/outlines.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

TEST(QmOutlineCache, NewMapPreparationStartsWithoutOldTilesOrDimensions)
{
	ivec2 Size(2, 2);
	std::vector<int> vData(4, 7);
	EXPECT_TRUE(QmPrepareOutlineMapCache(Size, vData, [&] {
		EXPECT_EQ(Size, ivec2(0, 0));
		EXPECT_TRUE(vData.empty());
		Size = {3, 1};
		vData = {1, 2, 3};
	}));
	EXPECT_EQ(Size, ivec2(3, 1));
	EXPECT_EQ(vData, (std::vector<int>{1, 2, 3}));
}

TEST(QmOutlineCache, NoEligibleLayerCannotKeepThePreviousMapsOutlines)
{
	ivec2 Size(2, 2);
	std::vector<int> vData(4, 7);
	EXPECT_TRUE(QmPrepareOutlineMapCache(Size, vData, [] {}));
	EXPECT_EQ(Size, ivec2(0, 0));
	EXPECT_TRUE(vData.empty());
}

TEST(QmOutlineCache, NativeVectorAllocationFailureDisablesAndReleasesTheOptionalCache)
{
	ivec2 Size(2, 2);
	std::vector<int> vData(4, 7);
	bool Prepared = true, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Prepared = QmPrepareOutlineMapCache(Size, vData, [&] {
			Size = {128, 128};
			vData.assign(128 * 128, 0);
		});
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_FALSE(Prepared);
	EXPECT_EQ(Size, ivec2(0, 0));
	EXPECT_TRUE(vData.empty());
	EXPECT_EQ(vData.capacity(), 0u);
}

TEST(QmOutlineCache, AllocationFailureAfterPartialFillDoesNotPublishPartialTiles)
{
	ivec2 Size(0, 0);
	std::vector<int> vData;
	EXPECT_FALSE(QmPrepareOutlineMapCache(Size, vData, [&] {
		Size = {4, 4};
		vData.assign(16, 3);
		throw std::bad_alloc();
	}));
	EXPECT_EQ(Size, ivec2(0, 0));
	EXPECT_TRUE(vData.empty());
	EXPECT_EQ(vData.capacity(), 0u);
}

TEST(QmOutlineCache, LengthFailureDisablesTheCacheWithoutEscaping)
{
	ivec2 Size(2, 2);
	std::vector<int> vData(4, 7);
	EXPECT_FALSE(QmPrepareOutlineMapCache(Size, vData, [&] {
		Size = {4, 4};
		throw std::length_error("outline cache preparation");
	}));
	EXPECT_EQ(Size, ivec2(0, 0));
	EXPECT_TRUE(vData.empty());
	EXPECT_EQ(vData.capacity(), 0u);
}

TEST(QmOutlineCache, UnrelatedPreparationFailureClearsTheCacheAndStillPropagates)
{
	ivec2 Size(2, 2);
	std::vector<int> vData(4, 7);
	EXPECT_THROW(QmPrepareOutlineMapCache(Size, vData, [&] {
		Size = {1, 1};
		vData.assign(1, 6);
		throw 17;
	}),
		int);
	EXPECT_EQ(Size, ivec2(0, 0));
	EXPECT_TRUE(vData.empty());
	EXPECT_EQ(vData.capacity(), 0u);
}

TEST(QmOutlineCache, ASubsequentMapCanBuildAfterResourceFailure)
{
	ivec2 Size(2, 2);
	std::vector<int> vData(4, 7);
	ASSERT_FALSE(QmPrepareOutlineMapCache(Size, vData, [] { throw std::bad_alloc(); }));
	ASSERT_TRUE(QmPrepareOutlineMapCache(Size, vData, [&] {
		Size = {1, 2};
		vData = {2, 4};
	}));
	EXPECT_EQ(Size, ivec2(1, 2));
	EXPECT_EQ(vData, (std::vector<int>{2, 4}));
}
