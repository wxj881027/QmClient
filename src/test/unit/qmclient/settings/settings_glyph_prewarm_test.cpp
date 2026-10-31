#include <game/client/QmUi/SettingsGlyphPrewarm.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <new>

namespace
{
	using TQueue = std::vector<std::pair<int, int>>;
}

TEST(SettingsGlyphPrewarm, PreparationDeduplicatesUnicodeInFirstAppearanceOrder)
{
	TQueue Queue{{99, 12}};
	size_t Cursor = 1, Plan = 3;
	ASSERT_TRUE(PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 7, "AA\xE4\xB8\xAD\xE4\xB8\xAD\xF0\x9F\x99\x82"
								      "B",
		18));
	EXPECT_EQ(Queue, (TQueue{{'A', 18}, {0x4e2d, 18}, {0x1f642, 18}, {'B', 18}}));
	EXPECT_EQ(Cursor, 0u);
	EXPECT_EQ(Plan, 7u);
}

TEST(SettingsGlyphPrewarm, InvalidUtf8IsSkippedAndTruncatedSuffixTerminates)
{
	TQueue Queue;
	size_t Cursor = 0, Plan = 0;
	ASSERT_TRUE(PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 1, "\xFF"
								      "A\xE4\xB8",
		18));
	EXPECT_EQ(Queue, (TQueue{{'A', 18}}));
}

TEST(SettingsGlyphPrewarm, EmptyAndNullTextPublishAnEmptyPreparedQueue)
{
	TQueue Queue{{99, 12}};
	size_t Cursor = 1, Plan = 3;
	ASSERT_TRUE(PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 7, "", 18));
	EXPECT_TRUE(Queue.empty());
	EXPECT_EQ(Cursor, 0u);
	EXPECT_EQ(Plan, 7u);
	ASSERT_TRUE(PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 8, nullptr, 18));
	EXPECT_TRUE(Queue.empty());
	EXPECT_EQ(Plan, 8u);
}

TEST(SettingsGlyphPrewarm, InvalidPixelSizeLeavesPreviousQueueAndOwnershipUnchanged)
{
	TQueue Queue{{99, 12}};
	size_t Cursor = 1, Plan = 3;
	EXPECT_FALSE(PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 7, "new", 0));
	EXPECT_EQ(Queue, (TQueue{{99, 12}}));
	EXPECT_EQ(Cursor, 1u);
	EXPECT_EQ(Plan, 3u);
}

TEST(SettingsGlyphPrewarm, EveryPreparationAllocationFailurePreservesPreviousQueueAndCanRetry)
{
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 256; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		TQueue Queue{{99, 12}, {100, 12}};
		size_t Cursor = 1, Plan = 3;
		bool Prepared, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Prepared = PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 7, "abcabc\xE4\xB8\xAD", 18);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Prepared);
			ReachedSuccess = true;
			break;
		}
		EXPECT_FALSE(Prepared);
		EXPECT_EQ(Queue, (TQueue{{99, 12}, {100, 12}}));
		EXPECT_EQ(Cursor, 1u);
		EXPECT_EQ(Plan, 3u);
		++Failures;
		ASSERT_TRUE(PrepareSettingsGlyphQueue(Queue, Cursor, Plan, 7, "abcabc\xE4\xB8\xAD", 18));
		EXPECT_EQ(Queue, (TQueue{{'a', 18}, {'b', 18}, {'c', 18}, {0x4e2d, 18}}));
		EXPECT_EQ(Cursor, 0u);
		EXPECT_EQ(Plan, 7u);
	}
	EXPECT_TRUE(ReachedSuccess);
	EXPECT_GT(Failures, 0);
}

TEST(SettingsGlyphPrewarm, AdvanceHonorsPerCallBudgetAndContinuesTheSameQueue)
{
	const TQueue Queue{{'A', 12}, {'B', 12}, {'C', 12}};
	size_t Cursor = 0;
	std::array<int, 3> Calls{};
	int Count = 0;
	const auto Prewarm = [&](int Chr, int Size) { EXPECT_EQ(Size, 12); Calls.at(Count++) = Chr; };
	EXPECT_FALSE(AdvanceSettingsGlyphQueue(Queue, Cursor, 2, Prewarm));
	EXPECT_EQ(Cursor, 2u);
	EXPECT_TRUE(AdvanceSettingsGlyphQueue(Queue, Cursor, 2, Prewarm));
	EXPECT_EQ(Cursor, 3u);
	EXPECT_EQ(Calls, (std::array<int, 3>{'A', 'B', 'C'}));
}

TEST(SettingsGlyphPrewarm, NonpositiveBudgetDoesNotInvokePrewarmingOrAdvance)
{
	const TQueue Queue{{'A', 12}};
	size_t Cursor = 0;
	int Calls = 0;
	EXPECT_FALSE(AdvanceSettingsGlyphQueue(Queue, Cursor, 0, [&](int, int) { ++Calls; }));
	EXPECT_FALSE(AdvanceSettingsGlyphQueue(Queue, Cursor, -1, [&](int, int) { ++Calls; }));
	EXPECT_EQ(Cursor, 0u);
	EXPECT_EQ(Calls, 0);
}

TEST(SettingsGlyphPrewarm, CallbackExceptionKeepsCurrentGlyphAndSuccessfulPrefixForRetry)
{
	const TQueue Queue{{'A', 12}, {'B', 12}, {'C', 12}};
	size_t Cursor = 0;
	std::array<int, 4> Calls{};
	int Count = 0;
	bool Fail = true;
	const auto Prewarm = [&](int Chr, int) {
		Calls.at(Count++) = Chr;
		if(Chr == 'B' && std::exchange(Fail, false))
			throw 7;
	};
	EXPECT_FALSE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, Prewarm));
	EXPECT_EQ(Cursor, 1u);
	EXPECT_TRUE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, Prewarm));
	EXPECT_EQ(Cursor, 3u);
	EXPECT_EQ(Calls, (std::array<int, 4>{'A', 'B', 'B', 'C'}));
}

TEST(SettingsGlyphPrewarm, AllocationExceptionFromCallbackDoesNotEscapeOrConsumeTheGlyph)
{
	const TQueue Queue{{'A', 12}};
	size_t Cursor = 0;
	EXPECT_FALSE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, [](int, int) { throw std::bad_alloc(); }));
	EXPECT_EQ(Cursor, 0u);
	EXPECT_TRUE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, [](int, int) {}));
	EXPECT_EQ(Cursor, 1u);
}

TEST(SettingsGlyphPrewarm, CompletedQueueCanBeAdvancedRepeatedlyWithoutMoreCalls)
{
	const TQueue Queue{{'A', 12}};
	size_t Cursor = Queue.size();
	int Calls = 0;
	EXPECT_TRUE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, [&](int, int) { ++Calls; }));
	EXPECT_TRUE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, [&](int, int) { ++Calls; }));
	EXPECT_EQ(Calls, 0);
}

TEST(SettingsGlyphPrewarm, CursorPastTheQueueDoesNotReadOutOfBounds)
{
	const TQueue Queue{{'A', 12}};
	size_t Cursor = 9;
	int Calls = 0;
	EXPECT_TRUE(AdvanceSettingsGlyphQueue(Queue, Cursor, 24, [&](int, int) { ++Calls; }));
	EXPECT_EQ(Cursor, 9u);
	EXPECT_EQ(Calls, 0);
}

TEST(SettingsGlyphPrewarm, PixelSizeTruncatesNormalScalingAndKeepsSubpixelTextAtOnePixel)
{
	EXPECT_EQ(SettingsGlyphPixelSize(12.5f, 600, 1200), 25);
	EXPECT_EQ(SettingsGlyphPixelSize(12.9f, 600, 600), 12);
	EXPECT_EQ(SettingsGlyphPixelSize(0.1f, 600, 100), 1);
}

TEST(SettingsGlyphPrewarm, PixelSizeUsesTheTextContainersFallbackForInvalidGeometry)
{
	const float Inf = std::numeric_limits<float>::infinity();
	EXPECT_EQ(SettingsGlyphPixelSize(12, 0, 1200), 12);
	EXPECT_EQ(SettingsGlyphPixelSize(12, Inf, 1200), 12);
	EXPECT_EQ(SettingsGlyphPixelSize(12, 600, 0), 1);
	EXPECT_EQ(SettingsGlyphPixelSize(12, 600, Inf), 1);
}

TEST(SettingsGlyphPrewarm, UnrepresentablePixelSizesAreRejectedBeforeIntegerConversion)
{
	EXPECT_FALSE(SettingsGlyphPixelSize(0, 1, 1));
	EXPECT_FALSE(SettingsGlyphPixelSize(-1, 1, 1));
	EXPECT_FALSE(SettingsGlyphPixelSize(std::numeric_limits<float>::quiet_NaN(), 1, 1));
	EXPECT_FALSE(SettingsGlyphPixelSize(std::numeric_limits<float>::infinity(), 1, 1));
	EXPECT_FALSE(SettingsGlyphPixelSize(std::numeric_limits<float>::max(), 1, 2));
	EXPECT_FALSE(SettingsGlyphPixelSize(static_cast<float>(std::numeric_limits<int>::max()), 1, 1));
	EXPECT_EQ(SettingsGlyphPixelSize(1000000000.0f, 1, 1), 1000000000);
}
