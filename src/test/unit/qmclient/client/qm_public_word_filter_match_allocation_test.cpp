#include <game/client/components/qmclient/public_word_filter.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>
#include <vector>

TEST(QmPublicWordFilterAllocation, FailedMatchPreservesBothOutputsAndAllowsRetry)
{
	const std::string FirstWord(32, 'a');
	const std::string SecondWord(32, 'b');
	const std::string Prefix = "A long unfiltered prefix: ";
	const std::string Original = Prefix + FirstWord + " " + SecondWord;
	const std::string Filtered = Prefix + "* *";
	const std::vector<std::string> PreviousMatches{"existing"};
	const std::vector<std::string> ExpectedMatches{"existing", FirstWord, SecondWord};
	QmPublicWordFilter::CState State;
	State.SetEnabled(true);
	ASSERT_TRUE(State.Restore(FirstWord + "\n" + SecondWord));

	for(int Allocation = 0; Allocation < 32; ++Allocation)
	{
		std::string Text = Original;
		std::vector<std::string> Matches = PreviousMatches;
		bool Matched, Triggered;
		{
			CPredictionAllocationFailure Failure(Allocation);
			Matched = State.Filter(Text, "*", false, &Matches);
			Triggered = Failure.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_GT(Allocation, 0);
			EXPECT_TRUE(Matched);
			EXPECT_EQ(Text, Filtered);
			EXPECT_EQ(Matches, ExpectedMatches);
			return;
		}

		EXPECT_FALSE(Matched);
		EXPECT_EQ(Text, Original);
		EXPECT_EQ(Matches, PreviousMatches);
		ASSERT_TRUE(State.Filter(Text, "*", false, &Matches));
		EXPECT_EQ(Text, Filtered);
		EXPECT_EQ(Matches, ExpectedMatches);
	}
	ADD_FAILURE() << "The allocation sweep did not reach a successful match";
}
