#include <game/client/components/qmclient/bind_editor.h>

#include <gtest/gtest.h>

using namespace qm_bind_editor;

TEST(QmBindEditor, AppendsActionsInOrderWithoutEmptySeparators)
{
	std::string Result;
	ASSERT_TRUE(AppendCommand("q", "", "+fire", Result));
	EXPECT_EQ(Result, "+fire");
	ASSERT_TRUE(AppendCommand("q", "+fire; ;", "+hook; +jump", Result));
	EXPECT_EQ(Result, "+fire; +hook; +jump");
}

TEST(QmBindEditor, NestedCommandsAndQuotedSeparatorsRemainOneAction)
{
	const auto Parsed = SplitCommands(R"(bind mouse1 "+fire; echo \"hello;world\""; say "#not a comment")");
	ASSERT_TRUE(Parsed.m_Complete);
	ASSERT_EQ(Parsed.m_vCommands.size(), 2u);
	const std::string ExpectedNested = R"(bind mouse1 "+fire; echo \"hello;world\"")";
	EXPECT_EQ(Parsed.m_vCommands[0], ExpectedNested);
	EXPECT_EQ(Parsed.m_vCommands[1], R"(say "#not a comment")");
}

TEST(QmBindEditor, AppendingAfterCommentDoesNotHideNewAction)
{
	std::string Result;
	ASSERT_TRUE(AppendCommand("q", "+fire; # old note", "+hook", Result));
	EXPECT_EQ(Result, "+fire; +hook");
}

TEST(QmBindEditor, PreservesTrailingSpacesUsedByChatPrefill)
{
	const auto Parsed = SplitCommands("+show_chat; chat all /c ");
	ASSERT_EQ(Parsed.m_vCommands.size(), 2u);
	EXPECT_EQ(JoinCommands(Parsed.m_vCommands), "+show_chat; chat all /c ");
}

TEST(QmBindEditor, IncompleteQuotedActionDoesNotChangeResult)
{
	std::string Result = "unchanged";
	EXPECT_FALSE(AppendCommand("q", "+fire", "say \"unfinished", Result));
	EXPECT_EQ(Result, "unchanged");
	EXPECT_FALSE(AppendCommand("q", "+fire", " ; ", Result));
	EXPECT_EQ(Result, "unchanged");
}

TEST(QmBindEditor, EscapesEachNestedBindLevel)
{
	const std::string Nested = BindCommand("mouse1", "+fire; +hook");
	EXPECT_EQ(Nested, R"(bind mouse1 "+fire; +hook")");
	const std::string ExpectedNested = R"(bind q "bind mouse1 \"+fire; +hook\"")";
	EXPECT_EQ(BindCommand("q", Nested), ExpectedNested);
	EXPECT_EQ(QuoteArgument(R"(exec cfg\test.cfg)"), R"("exec cfg\\test.cfg")");
}

TEST(QmBindEditor, AllowsLongUtf8ActionsWithoutTruncation)
{
	const std::string Action = "echo " + std::string(4096, 'a') + " 中文";
	std::string Result;
	ASSERT_TRUE(AppendCommand("ctrl+q", "+fire", Action, Result));
	EXPECT_EQ(Result, "+fire; " + Action);
	EXPECT_GT(Result.size(), 255u);
}

TEST(QmBindEditor, LimitsSerializedBytesIncludingEscapesAndKeyName)
{
	const size_t Overhead = BindCommand("ctrl+shift+q", "").size();
	const std::string AtLimit(MAX_CONFIG_LINE_BYTES - Overhead, 'x');
	EXPECT_TRUE(FitsConfigLine("ctrl+shift+q", AtLimit));
	EXPECT_FALSE(FitsConfigLine("ctrl+shift+q", AtLimit + "x"));
	EXPECT_FALSE(FitsConfigLine("q", std::string(MAX_CONFIG_LINE_BYTES / 2, '\\')));
	EXPECT_FALSE(FitsConfigLine("q", "echo a\necho b"));
	EXPECT_FALSE(FitsConfigLine("q", std::string("echo a\0b", 8)));
}

TEST(QmBindEditor, RemovingAnActionPreservesNestedNeighbors)
{
	auto Parsed = SplitCommands(R"(+jump; bind mouse1 "+fire; +hook"; echo done)");
	ASSERT_EQ(Parsed.m_vCommands.size(), 3u);
	Parsed.m_vCommands.erase(Parsed.m_vCommands.begin());
	EXPECT_EQ(JoinCommands(Parsed.m_vCommands), R"(bind mouse1 "+fire; +hook"; echo done)");
}
