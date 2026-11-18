#include <game/client/components/qmclient/keyword_reply_rules.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

namespace
{
	int FirstChoice(int) { return 0; }
}

TEST(KeywordReplyRulesAllocation, FailedConfigRebuildSuppressesRepliesAndCanRetryTheSameConfig)
{
	constexpr char aNewConfig[] = "first=>first new reply\\n[rename] second=>second new reply";
	bool Completed = false;
	int FailedAttempts = 0;
	for(size_t BeforeFailure = 0; BeforeFailure < 64; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		QmKeywordReplyRules::CRuleSet Rules;
		Rules.SetConfig("old=>old reply");
		bool Triggered;
		{
			CPredictionAllocationFailure Fault(BeforeFailure);
			Rules.SetConfig(aNewConfig);
			Triggered = Fault.Triggered();
		}

		char aReply[64];
		bool Rename = false;
		if(Triggered)
		{
			++FailedAttempts;
			EXPECT_FALSE(Rules.Match("old", aReply, sizeof(aReply), Rename, FirstChoice));
			EXPECT_FALSE(Rules.Match("first", aReply, sizeof(aReply), Rename, FirstChoice));
			EXPECT_EQ(aReply[0], '\0');
			EXPECT_FALSE(Rename);
			Rules.SetConfig(aNewConfig);
		}
		EXPECT_FALSE(Rules.Match("old", aReply, sizeof(aReply), Rename, FirstChoice));
		ASSERT_TRUE(Rules.Match("first", aReply, sizeof(aReply), Rename, FirstChoice));
		EXPECT_STREQ(aReply, "first new reply");
		EXPECT_FALSE(Rename);
		ASSERT_TRUE(Rules.Match("second", aReply, sizeof(aReply), Rename, FirstChoice));
		EXPECT_STREQ(aReply, "second new reply");
		EXPECT_TRUE(Rename);
		if(!Triggered)
		{
			Completed = true;
			break;
		}
	}
	EXPECT_GT(FailedAttempts, 1);
	EXPECT_TRUE(Completed);
}

TEST(KeywordReplyRulesAllocation, FailedMatchGivesNoReplyAndKeepsTheCompiledRules)
{
	QmKeywordReplyRules::CRuleSet Rules;
	Rules.SetConfig("[rename] hello=>reply");
	char aReply[64] = "previous";
	bool Rename = true;
	bool Matched, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Matched = Rules.Match("hello", aReply, sizeof(aReply), Rename, FirstChoice);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Matched);
	EXPECT_EQ(aReply[0], '\0');
	EXPECT_FALSE(Rename);
	ASSERT_TRUE(Rules.Match("hello", aReply, sizeof(aReply), Rename, FirstChoice));
	EXPECT_STREQ(aReply, "reply");
	EXPECT_TRUE(Rename);
}
