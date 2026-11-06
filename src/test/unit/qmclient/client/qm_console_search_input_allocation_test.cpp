#include <game/client/components/qmclient/console_search_input.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>

TEST(QmConsoleSearchInputAllocation, FailedDraftCopyLeavesSearchInactiveAndCanRetry)
{
	CQmConsoleSearchInput Input;
	const std::string Command(128, 'x');
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Started = Input.Begin(Command.c_str());
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Started);
	EXPECT_FALSE(Input.IsSearching());
	EXPECT_FALSE(Input.End().has_value());
	ASSERT_TRUE(Input.Begin(Command.c_str()));
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, Command);
}

TEST(QmConsoleSearchInputAllocation, ReenteringActiveSearchDoesNotAllocateOrReplaceDraft)
{
	CQmConsoleSearchInput Input;
	const std::string Original(128, 'o'), Replacement(128, 'r');
	ASSERT_TRUE(Input.Begin(Original.c_str()));
	bool Started, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Started = Input.Begin(Replacement.c_str());
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_FALSE(Started);
	EXPECT_TRUE(Input.IsSearching());
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, Original);
}

TEST(QmConsoleSearchInputAllocation, LeavingSearchMovesDraftWithoutAllocating)
{
	CQmConsoleSearchInput Input;
	const std::string Original(128, 'o');
	ASSERT_TRUE(Input.Begin(Original.c_str()));
	std::optional<std::string> Draft;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		Draft = Input.End();
		Triggered = Fault.Triggered();
	}
	EXPECT_FALSE(Triggered);
	EXPECT_FALSE(Input.IsSearching());
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, Original);
	EXPECT_FALSE(Input.End().has_value());
}

TEST(QmConsoleSearchInputAllocation, SavedCommandDoesNotBorrowSourceInput)
{
	CQmConsoleSearchInput Input;
	std::string Command = "echo 未执行的草稿";
	ASSERT_TRUE(Input.Begin(Command.c_str()));
	Command.assign("different input");
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, "echo 未执行的草稿");
}
