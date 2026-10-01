#include <game/client/components/qmclient/bind_editor.h>

#include <gtest/gtest.h>

using namespace qm_bind_editor;

TEST(QmBindEditor, ParameterMetadataKeepsNamesAndOptionalSuffix)
{
	const auto Parameters = ParseParameters("s[key] r[command] ?i[mode] f[value]");
	ASSERT_EQ(Parameters.size(), 4u);
	EXPECT_EQ(Parameters[0].m_Type, 's');
	EXPECT_EQ(Parameters[0].m_Name, "key");
	EXPECT_FALSE(Parameters[0].m_Optional);
	EXPECT_EQ(Parameters[1].m_Name, "command");
	EXPECT_FALSE(Parameters[1].m_Optional);
	EXPECT_EQ(Parameters[2].m_Type, 'i');
	EXPECT_TRUE(Parameters[2].m_Optional);
	EXPECT_EQ(Parameters[3].m_Type, 'f');
	EXPECT_TRUE(Parameters[3].m_Optional);
}

TEST(QmBindEditor, OptionalArgumentsCanBeOmittedOrExplicitlyEmpty)
{
	const auto Parameters = ParseParameters("s[name] ?r[value]");
	std::string Command;
	ASSERT_TRUE(ComposeCommand("qm_option", Parameters, {"name", std::nullopt}, Command));
	EXPECT_EQ(Command, R"(qm_option "name")");
	ASSERT_TRUE(ComposeCommand("qm_option", Parameters, {"name", ""}, Command));
	EXPECT_EQ(Command, R"(qm_option "name" "")");
}

TEST(QmBindEditor, MissingRequiredOrMiddleArgumentsDoNotShiftLaterValues)
{
	const auto Parameters = ParseParameters("s[name] ?i[first] i[second]");
	std::string Command = "unchanged";
	EXPECT_FALSE(ComposeCommand("qm_option", Parameters, {std::nullopt, "1", "2"}, Command));
	EXPECT_EQ(Command, "unchanged");
	EXPECT_FALSE(ComposeCommand("qm_option", Parameters, {"name", std::nullopt, "2"}, Command));
	EXPECT_EQ(Command, "unchanged");
}

TEST(QmBindEditor, ParameterTextQuotesNestedCommandsWithoutSplittingThem)
{
	const auto Parameters = ParseParameters("s[key] r[command]");
	const std::string Action = R"(+fire; echo "a; b"; exec cfg\test.cfg)";
	std::string Command;
	ASSERT_TRUE(ComposeCommand("bind", Parameters, {"mouse1", Action}, Command));
	const auto Parsed = SplitCommands(Command);
	ASSERT_TRUE(Parsed.m_Complete);
	ASSERT_EQ(Parsed.m_vCommands.size(), 1u);
	EXPECT_EQ(Parsed.m_vCommands.front(), Command);
}

TEST(QmBindEditor, MultilineOrNulParametersLeaveThePreviousCommandIntact)
{
	const auto Parameters = ParseParameters("r[text]");
	std::string Command = "unchanged";
	EXPECT_FALSE(ComposeCommand("echo", Parameters, {"first\nsecond"}, Command));
	EXPECT_FALSE(ComposeCommand("echo", Parameters, {std::string("a\0b", 3)}, Command));
	EXPECT_EQ(Command, "unchanged");
}

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

TEST(QmBindEditor, MovingALongBindAccountsForTheDestinationKeyName)
{
	const std::string Command(MAX_CONFIG_LINE_BYTES - BindCommand("q", "").size(), 'x');
	EXPECT_TRUE(FitsConfigLine("q", Command));
	EXPECT_FALSE(FitsConfigLine("ctrl+alt+shift+q", Command));
}
