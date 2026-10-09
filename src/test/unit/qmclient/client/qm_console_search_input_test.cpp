#include <game/client/components/qmclient/console_search_input.h>

#include <gtest/gtest.h>

TEST(QmConsoleSearchInput, LeavingSearchRestoresUnexecutedCommand)
{
	CQmConsoleSearchInput Input;
	EXPECT_TRUE(Input.Begin("echo 中文草稿"));
	EXPECT_TRUE(Input.IsSearching());
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, "echo 中文草稿");
	EXPECT_FALSE(Input.IsSearching());
}

TEST(QmConsoleSearchInput, ReenteringSearchDoesNotReplaceOriginalCommand)
{
	CQmConsoleSearchInput Input;
	EXPECT_TRUE(Input.Begin("echo draft"));
	EXPECT_FALSE(Input.Begin("query"));
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, "echo draft");
}

TEST(QmConsoleSearchInput, RepeatedLeaveDoesNotRequestAnotherInputReplacement)
{
	CQmConsoleSearchInput Input;
	Input.Begin("echo draft");
	ASSERT_TRUE(Input.End().has_value());
	EXPECT_FALSE(Input.End().has_value());
}

TEST(QmConsoleSearchInput, NewSearchSessionRestoresTheNewCommandDraft)
{
	CQmConsoleSearchInput Input;
	Input.Begin("echo first");
	Input.End();
	Input.Begin("echo second");
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_EQ(*Draft, "echo second");
}

TEST(QmConsoleSearchInput, EmptyCommandStillEntersSearchAndRestoresEmptyInput)
{
	CQmConsoleSearchInput Input;
	Input.Begin("");
	EXPECT_TRUE(Input.IsSearching());
	const auto Draft = Input.End();
	ASSERT_TRUE(Draft.has_value());
	EXPECT_TRUE(Draft->empty());
}
