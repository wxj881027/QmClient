#include <game/client/QmUi/SettingsPageLayout.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <new>

TEST(SettingsChoiceWidthFailure, InitialAllocationFailuresReturnZeroAndRetryTheSameLabels)
{
	const std::vector<const char *> vLabels{"A long translated option with owned storage", "Another long translated option with owned storage"};
	const auto Measure = [](const char *, float) { return 160.0f; };
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 32; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CSettingsChoiceWidthCache Cache;
		float Width;
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Width = Cache.Resolve(vLabels, 12, 1, Measure);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			ReachedSuccess = Width == 160.0f;
			break;
		}
		++Failures;
		EXPECT_FLOAT_EQ(Width, 0);
		EXPECT_FLOAT_EQ(Cache.Resolve(vLabels, 12, 1, Measure), 160);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(ReachedSuccess);
}

TEST(SettingsChoiceWidthFailure, RefreshAllocationFailuresPreserveThePreviousWidthAndKey)
{
	const std::vector<const char *> vOld{"An old long translated option", "Another old long translated option"};
	const std::vector<const char *> vNew{"A new long translated option", "Another new long translated option"};
	bool ReachedSuccess = false;
	int Failures = 0;
	for(int Allocation = 0; Allocation < 32; ++Allocation)
	{
		SCOPED_TRACE(Allocation);
		CSettingsChoiceWidthCache Cache;
		ASSERT_FLOAT_EQ(Cache.Resolve(vOld, 12, 1, [](const char *, float) { return 80.0f; }), 80);
		float Width;
		bool Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Width = Cache.Resolve(vNew, 12, 1, [](const char *, float) { return 160.0f; });
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			ReachedSuccess = Width == 160.0f;
			break;
		}
		++Failures;
		EXPECT_FLOAT_EQ(Width, 80);
		int Measures = 0;
		EXPECT_FLOAT_EQ(Cache.Resolve(vOld, 12, 1, [&](const char *, float) { ++Measures; return 999.0f; }), 80);
		EXPECT_EQ(Measures, 0);
		EXPECT_FLOAT_EQ(Cache.Resolve(vNew, 12, 1, [](const char *, float) { return 160.0f; }), 160);
	}
	EXPECT_GT(Failures, 0);
	EXPECT_TRUE(ReachedSuccess);
}

TEST(SettingsChoiceWidthFailure, FirstMeasurementExceptionLeavesAnUninitializedCacheRetryable)
{
	CSettingsChoiceWidthCache Cache;
	const char *apLabels[] = {"A", "B"};
	EXPECT_FLOAT_EQ(Cache.Resolve(apLabels, 2, 12, 1, [](const char *, float) -> float { throw std::bad_alloc(); }), 0);
	int Measures = 0;
	EXPECT_FLOAT_EQ(Cache.Resolve(apLabels, 2, 12, 1, [&](const char *, float) { ++Measures; return 120.0f; }), 120);
	EXPECT_EQ(Measures, 2);
}

TEST(SettingsChoiceWidthFailure, LastMeasurementExceptionDoesNotPublishNewLabelsWithAPartialWidth)
{
	CSettingsChoiceWidthCache Cache;
	const char *apOld[] = {"Old A", "Old B"};
	const char *apNew[] = {"New A", "New B"};
	ASSERT_FLOAT_EQ(Cache.Resolve(apOld, 2, 12, 1, [](const char *, float) { return 80.0f; }), 80);
	int Measures = 0;
	EXPECT_FLOAT_EQ(Cache.Resolve(apNew, 2, 12, 1, [&](const char *, float) {
		if(++Measures == 2)
			throw std::bad_alloc();
		return 120.0f;
	}),
		80);
	EXPECT_EQ(Measures, 2);
	Measures = 0;
	EXPECT_FLOAT_EQ(Cache.Resolve(apNew, 2, 12, 1, [&](const char *, float) { ++Measures; return 240.0f; }), 240);
	EXPECT_EQ(Measures, 2);
}

TEST(SettingsChoiceWidthFailure, FailedFontRevisionMeasurementKeepsTheOldRevisionCached)
{
	CSettingsChoiceWidthCache Cache;
	const std::vector<const char *> vLabels{"A", "B"};
	ASSERT_FLOAT_EQ(Cache.Resolve(vLabels, 12, 1, [](const char *, float) { return 80.0f; }), 80);
	EXPECT_FLOAT_EQ(Cache.Resolve(vLabels, 12, 2, [](const char *, float) -> float { throw std::bad_alloc(); }), 80);
	int Measures = 0;
	EXPECT_FLOAT_EQ(Cache.Resolve(vLabels, 12, 1, [&](const char *, float) { ++Measures; return 999.0f; }), 80);
	EXPECT_EQ(Measures, 0);
	EXPECT_FLOAT_EQ(Cache.Resolve(vLabels, 12, 2, [](const char *, float) { return 160.0f; }), 160);
}

TEST(SettingsChoiceWidthFailure, CacheHitNeitherAllocatesNorMeasures)
{
	CSettingsChoiceWidthCache Cache;
	const std::vector<const char *> vLabels{"A long translated option with owned storage"};
	ASSERT_FLOAT_EQ(Cache.Resolve(vLabels, 12, 1, [](const char *, float) { return 80.0f; }), 80);
	int Measures = 0;
	bool Triggered;
	float Width;
	{
		CPredictionAllocationFailure Failure;
		Width = Cache.Resolve(vLabels, 12, 1, [&](const char *, float) { ++Measures; return 999.0f; });
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Measures, 0);
	EXPECT_FLOAT_EQ(Width, 80);
}
