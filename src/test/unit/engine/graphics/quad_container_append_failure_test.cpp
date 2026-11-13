#include <engine/client/quad_container_render.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

TEST(QuadContainerAppend, SuccessfulBatchReturnsItsOffsetAndPreservesThePrefix)
{
	std::vector<int> vQuads{7, 8};
	const int aValues[]{10, 11, 12};
	int Calls = 0;
	EXPECT_EQ(AppendGraphicsQuads(vQuads, 3, [&](int &Quad, int i) {
		Quad = aValues[i];
		++Calls;
	}),
		2);
	EXPECT_EQ(vQuads, (std::vector<int>{7, 8, 10, 11, 12}));
	EXPECT_EQ(Calls, 3);
}

TEST(QuadContainerAppend, EmptyBatchReturnsTheCurrentOffsetWithoutAllocationOrFilling)
{
	std::vector<int> vQuads{7, 8};
	int Offset = -1, Calls = 0;
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		Offset = AppendGraphicsQuads(vQuads, 0, [&](int &, int) { ++Calls; });
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_EQ(Offset, 2);
	EXPECT_EQ(vQuads, (std::vector<int>{7, 8}));
	EXPECT_EQ(Calls, 0);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
}

TEST(QuadContainerAppend, InvalidCountsAreRejectedWithoutAllocationOrChangingData)
{
	for(int Count : {-1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Count);
		std::vector<int> vQuads{7, 8};
		int Offset = 0, Calls = 0;
		bool Triggered = false;
		size_t Attempts = 0;
		{
			CPredictionAllocationFailure Failure;
			Offset = AppendGraphicsQuads(vQuads, Count, [&](int &, int) { ++Calls; });
			Triggered = Failure.Triggered();
			Attempts = Failure.Attempts();
		}
		EXPECT_EQ(Offset, -1);
		EXPECT_EQ(vQuads, (std::vector<int>{7, 8}));
		EXPECT_EQ(Calls, 0);
		EXPECT_FALSE(Triggered);
		EXPECT_EQ(Attempts, 0u);
	}
}

TEST(QuadContainerAppend, GrowthFailureBeforeFillingPreservesSizeStorageAndData)
{
	std::vector<int> vQuads{7, 8};
	vQuads.reserve(4);
	ASSERT_LT(vQuads.capacity(), GRAPHICS_MAX_CONTAINER_QUADS);
	const int Count = static_cast<int>(vQuads.capacity() - vQuads.size() + 1);
	const int *pQuads = vQuads.data();
	int Calls = 0;
	bool Threw = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			AppendGraphicsQuads(vQuads, Count, [&](int &Quad, int) {
				Quad = 10;
				++Calls;
			});
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Threw);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(vQuads.data(), pQuads);
	EXPECT_EQ(vQuads, (std::vector<int>{7, 8}));
}

TEST(QuadContainerAppend, FillExceptionRemovesTheWholeNewBatchAndPropagates)
{
	std::vector<int> vQuads{7, 8};
	int Calls = 0;
	EXPECT_THROW(AppendGraphicsQuads(vQuads, 3, [&](int &Quad, int i) {
		++Calls;
		if(i == 1)
			throw std::length_error("quad fill");
		Quad = 10;
	}),
		std::length_error);
	EXPECT_EQ(Calls, 2);
	EXPECT_EQ(vQuads, (std::vector<int>{7, 8}));
}

TEST(QuadContainerAppend, UnknownFillExceptionRollsBackWithoutAllocating)
{
	std::vector<int> vQuads{7, 8};
	vQuads.reserve(8);
	const int *pQuads = vQuads.data();
	int Error = 0, Calls = 0;
	bool Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			AppendGraphicsQuads(vQuads, 3, [&](int &Quad, int i) {
				++Calls;
				if(i == 1)
					throw 17;
				Quad = 10;
			});
		}
		catch(int Value)
		{
			Error = Value;
		}
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_EQ(Error, 17);
	EXPECT_EQ(Calls, 2);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Attempts, 0u);
	EXPECT_EQ(vQuads.data(), pQuads);
	EXPECT_EQ(vQuads, (std::vector<int>{7, 8}));
}

TEST(QuadContainerAppend, RetryAfterGrowthFailureAppendsExactlyOnce)
{
	std::vector<int> vQuads{7, 8};
	vQuads.reserve(4);
	ASSERT_LT(vQuads.capacity(), GRAPHICS_MAX_CONTAINER_QUADS);
	const int Count = static_cast<int>(vQuads.capacity() - vQuads.size() + 1);
	const std::vector<int> vValues(static_cast<size_t>(Count), 10);
	std::vector<int> vExpected{7, 8};
	vExpected.insert(vExpected.end(), vValues.begin(), vValues.end());
	int Calls = 0;
	const auto Fill = [&](int &Quad, int i) {
		Quad = vValues[i];
		++Calls;
	};
	bool Threw = false, Triggered = false;
	size_t Attempts = 0;
	{
		CPredictionAllocationFailure Failure;
		try
		{
			AppendGraphicsQuads(vQuads, Count, Fill);
		}
		catch(const std::bad_alloc &)
		{
			Threw = true;
		}
		Triggered = Failure.Triggered();
		Attempts = Failure.Attempts();
	}
	EXPECT_TRUE(Threw);
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Attempts, 1u);
	EXPECT_EQ(Calls, 0);
	EXPECT_EQ(AppendGraphicsQuads(vQuads, Count, Fill), 2);
	EXPECT_EQ(Calls, Count);
	EXPECT_EQ(vQuads, vExpected);
}
