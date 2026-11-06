#include <game/client/components/qmclient/streamer_word_filter.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace
{
	using QmStreamerWordFilter::CFilter;

	size_t CountPublicationAllocations(const std::vector<std::string> &vWords)
	{
		CFilter Filter;
		bool Published;
		size_t Attempts;
		{
			CPredictionAllocationFailure Observe(std::numeric_limits<size_t>::max());
			Published = Filter.SetAdditionalWords(vWords);
			Attempts = Observe.Attempts();
		}
		EXPECT_TRUE(Published);
		return Attempts;
	}

	std::array<size_t, 4> FailurePositions(size_t Count)
	{
		return {0, 1, Count / 2, Count - 1};
	}
}

TEST(QmStreamerFilterAllocation, FirstRevisionPreparationFailureReturnsUnavailableAndRetries)
{
	CFilter Filter;
	uint64_t Revision;
	bool Triggered;
	{
		CPredictionAllocationFailure Failure;
		Revision = Filter.Revision();
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_EQ(Revision, 0u);
	EXPECT_EQ(Filter.Revision(), 1u);
	std::string Text = "习近平";
	EXPECT_TRUE(Filter.Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerFilterAllocation, FirstFilteringPreparationFailurePreservesInputAndRetries)
{
	CFilter Filter;
	std::string Text = "习近平";
	bool Changed, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Changed = Filter.Filter(Text, true);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Changed);
	EXPECT_EQ(Text, "习近平");
	EXPECT_TRUE(Filter.Filter(Text, true));
	EXPECT_EQ(Text, "***");
}

TEST(QmStreamerFilterAllocation, InitialPublicationPreparationFailuresAllowACompleteRetry)
{
	const std::vector<std::string> Words{"qzhealthnewtoken"};
	const size_t Count = CountPublicationAllocations(Words);
	ASSERT_GT(Count, 2u);
	for(size_t Position : FailurePositions(Count))
	{
		SCOPED_TRACE(Position);
		CFilter Filter;
		bool Published, Triggered;
		{
			CPredictionAllocationFailure Failure(Position);
			Published = Filter.SetAdditionalWords(Words);
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Published);
		ASSERT_TRUE(Filter.SetAdditionalWords(Words));
		EXPECT_EQ(Filter.Revision(), 2u);
		std::string Text = "qzhealthnewtoken 习近平";
		EXPECT_TRUE(Filter.Filter(Text, true));
		EXPECT_EQ(Text, "*** ***");
	}
}

TEST(QmStreamerFilterAllocation, ReplacementPreparationFailuresKeepTheOldRulesAndRevision)
{
	const std::vector<std::string> OldWords{"qzhealtholdtoken"};
	const std::vector<std::string> NewWords{"qzhealthnewtoken"};
	const size_t Count = CountPublicationAllocations(NewWords);
	ASSERT_GT(Count, 2u);
	for(size_t Position : FailurePositions(Count))
	{
		SCOPED_TRACE(Position);
		CFilter Filter;
		ASSERT_TRUE(Filter.SetAdditionalWords(OldWords));
		const uint64_t Before = Filter.Revision();
		bool Published, Triggered;
		{
			CPredictionAllocationFailure Failure(Position);
			Published = Filter.SetAdditionalWords(NewWords);
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Published);
		EXPECT_EQ(Filter.Revision(), Before);
		std::string Text = "qzhealtholdtoken qzhealthnewtoken 习近平";
		EXPECT_TRUE(Filter.Filter(Text, true));
		EXPECT_EQ(Text, "*** qzhealthnewtoken ***");
		ASSERT_TRUE(Filter.SetAdditionalWords(NewWords));
		EXPECT_EQ(Filter.Revision(), Before + 1);
		Text = "qzhealtholdtoken qzhealthnewtoken 习近平";
		EXPECT_TRUE(Filter.Filter(Text, true));
		EXPECT_EQ(Text, "qzhealtholdtoken *** ***");
	}
}

TEST(QmStreamerFilterAllocation, FailedClearKeepsExternalRulesUntilTheRetryPublishes)
{
	const size_t Count = CountPublicationAllocations({});
	ASSERT_GT(Count, 2u);
	for(size_t Position : FailurePositions(Count))
	{
		SCOPED_TRACE(Position);
		CFilter Filter;
		ASSERT_TRUE(Filter.SetAdditionalWords({"qzhealtholdtoken"}));
		const uint64_t Before = Filter.Revision();
		bool Published, Triggered;
		{
			CPredictionAllocationFailure Failure(Position);
			Published = Filter.SetAdditionalWords({});
			Triggered = Failure.Triggered();
		}
		EXPECT_TRUE(Triggered);
		EXPECT_FALSE(Published);
		EXPECT_EQ(Filter.Revision(), Before);
		std::string Text = "qzhealtholdtoken 习近平";
		EXPECT_TRUE(Filter.Filter(Text, true));
		EXPECT_EQ(Text, "*** ***");
		ASSERT_TRUE(Filter.SetAdditionalWords({}));
		Text = "qzhealtholdtoken 习近平";
		EXPECT_TRUE(Filter.Filter(Text, true));
		EXPECT_EQ(Text, "qzhealtholdtoken ***");
	}
}

TEST(QmStreamerFilterAllocation, ReapplyingIdenticalWordsDoesNotAllocateOrAdvanceRevision)
{
	CFilter Filter;
	const std::vector<std::string> Words{"qzhealtholdtoken"};
	ASSERT_TRUE(Filter.SetAdditionalWords(Words));
	const uint64_t Before = Filter.Revision();
	bool Published, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Published = Filter.SetAdditionalWords(Words);
		Triggered = Failure.Triggered();
	}
	EXPECT_TRUE(Published);
	EXPECT_FALSE(Triggered);
	EXPECT_EQ(Filter.Revision(), Before);
}

TEST(QmStreamerFilterAllocation, DisabledNullAndEmptyInputDoNotPrepareAFilter)
{
	CFilter Filter;
	char aText[] = "习近平";
	char aEmpty[] = "";
	bool Disabled, Null, Empty, Triggered;
	{
		CPredictionAllocationFailure Failure;
		Disabled = Filter.Filter(aText, false);
		Null = Filter.Filter(nullptr, true);
		Empty = Filter.Filter(aEmpty, true);
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_FALSE(Disabled);
	EXPECT_FALSE(Null);
	EXPECT_FALSE(Empty);
	EXPECT_STREQ(aText, "习近平");
}

TEST(QmStreamerFilterAllocation, ReadyFilterUsesBothBufferInterfacesWithoutAllocating)
{
	CFilter Filter;
	ASSERT_TRUE(Filter.SetAdditionalWords({"qzhealtholdtoken"}));
	char aText[] = "qzhealtholdtoken";
	std::string Text = "习近平";
	bool RawChanged, StringChanged, Triggered;
	{
		CPredictionAllocationFailure Failure;
		RawChanged = Filter.Filter(aText, true);
		StringChanged = Filter.Filter(Text, true);
		Triggered = Failure.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_TRUE(RawChanged);
	EXPECT_TRUE(StringChanged);
	EXPECT_STREQ(aText, "***");
	EXPECT_EQ(Text, "***");
}
