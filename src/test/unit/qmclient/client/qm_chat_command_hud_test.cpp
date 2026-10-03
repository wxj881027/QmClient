#include <game/client/components/qmclient/chat_command_hud.h>

#include <gtest/gtest.h>

namespace
{
	void AddServerCandidates(CQmChatCommandHud &Hud, const char *pInput, size_t Cursor)
	{
		Hud.BeginUpdate(pInput, Cursor, true, 1);
		Hud.AddServerCommand("rank", "?r[player]", "Show rank");
		Hud.AddServerCommand("race", "", "Show time");
		Hud.AddServerCommand("team", "?i[team]", "Join a team");
		Hud.EndUpdate();
		Hud.SetLayout(10.0f, 250.0f, 200.0f, 200.0f, 8.0f);
	}

	float RowCenterY(const CQmChatCommandHud &Hud, int Row)
	{
		const auto &Layout = Hud.Layout();
		return Layout.m_Y + Layout.m_HeaderHeight + (Row + 0.5f) * Layout.m_RowHeight;
	}
}

TEST(QmChatCommandHud, ServerPrefixMatchingIsCaseInsensitiveAndKeepsMetadata)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/RA", 3);
	ASSERT_EQ(Hud.Count(), 2);
	EXPECT_EQ(Hud.Candidate(0).m_Name, "/race");
	EXPECT_EQ(Hud.Candidate(1).m_Name, "/rank");
	EXPECT_EQ(Hud.Candidate(1).m_Detail, "?r[player]  Show rank");
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::SERVER);
}

TEST(QmChatCommandHud, EmptyOrPlainInputDoesNotShowCandidates)
{
	CQmChatCommandHud Hud;
	for(const char *pInput : {"", "!w", "hello"})
	{
		SCOPED_TRACE(pInput);
		AddServerCandidates(Hud, "/ra", 3);
		AddServerCandidates(Hud, pInput, str_length(pInput));
		EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::NONE);
		EXPECT_EQ(Hud.Count(), 0);
		EXPECT_EQ(Hud.Layout().m_VisibleRows, 0);
	}
}

TEST(QmChatCommandHud, ReplacingCommandPreservesExistingArguments)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra \"Player name\"", 3);
	const float Y = RowCenterY(Hud, 1);
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.Press(20.0f, Y, "/ra \"Player name\"", 3));
	ASSERT_TRUE(Hud.Release(20.0f, Y, "/ra \"Player name\"", 3, Completion, Cursor));
	EXPECT_EQ(Completion, "/rank \"Player name\"");
	EXPECT_EQ(Cursor, 5U);
}

TEST(QmChatCommandHud, ArgumentCursorLeavesPlayerAndEmojiCompletionAlone)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/rank Player", 12);
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::NONE);
	EXPECT_EQ(Hud.Count(), 0);
	EXPECT_EQ(Hud.Layout().m_VisibleRows, 0);
}

TEST(QmChatCommandHud, ReleaseRequiresPressOnSameCandidate)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	const float FirstY = RowCenterY(Hud, 0);
	const float SecondY = RowCenterY(Hud, 1);
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.Press(0.0f, FirstY, "/ra", 3));
	EXPECT_FALSE(Hud.Release(20.0f, FirstY, "/ra", 3, Completion, Cursor));
	ASSERT_TRUE(Hud.Press(20.0f, FirstY, "/ra", 3));
	EXPECT_FALSE(Hud.Release(20.0f, SecondY, "/ra", 3, Completion, Cursor));
	EXPECT_TRUE(Completion.empty());
}

TEST(QmChatCommandHud, EditingOrHidingBetweenPressAndReleaseCancelsSelection)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	const float Y = RowCenterY(Hud, 0);
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.Press(20.0f, Y, "/ra", 3));
	EXPECT_FALSE(Hud.Release(20.0f, Y, "/rac", 4, Completion, Cursor));
	ASSERT_TRUE(Hud.Press(20.0f, Y, "/ra", 3));
	Hud.Hide();
	EXPECT_FALSE(Hud.Release(20.0f, Y, "/ra", 3, Completion, Cursor));
}

TEST(QmChatCommandHud, LayoutRespectsAvailableHeightAndScaledMouseCoordinates)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	Hud.SetLayout(10.0f, 100.0f, 200.0f, 13.0f, 8.0f);
	ASSERT_EQ(Hud.Layout().m_VisibleRows, 1);
	EXPECT_FLOAT_EQ(Hud.Layout().m_W, 80.0f);
	EXPECT_GE(Hud.Layout().m_Y, 87.0f);
	Hud.SetInputTransform(30.0f, -10.0f, 2.0f);
	EXPECT_EQ(Hud.HoveredRow(70.0f, RowCenterY(Hud, 0) * 2.0f - 10.0f), 0);
	EXPECT_FALSE(Hud.Contains(211.0f, RowCenterY(Hud, 0) * 2.0f - 10.0f));
	EXPECT_FALSE(Hud.Contains(20.0f, RowCenterY(Hud, 0)));
	Hud.SetLayout(10.0f, 100.0f, 200.0f, 10.0f, 8.0f);
	EXPECT_EQ(Hud.Layout().m_VisibleRows, 0);
}

TEST(QmChatCommandHud, ScrollingKeepsAllCandidatesReachableAndCancelsPressedRow)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/", 1);
	Hud.SetLayout(10.0f, 100.0f, 200.0f, 13.0f, 8.0f);
	const float Y = RowCenterY(Hud, 0);
	ASSERT_TRUE(Hud.Press(20.0f, Y, "/", 1));
	ASSERT_TRUE(Hud.Scroll(20.0f, Y, 20, "/", 1));
	EXPECT_EQ(Hud.Offset(), 2);
	EXPECT_FALSE(Hud.IsPressed());
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.Press(20.0f, Y, "/", 1));
	ASSERT_TRUE(Hud.Release(20.0f, Y, "/", 1, Completion, Cursor));
	EXPECT_EQ(Completion, "/team ");
	EXPECT_FALSE(Hud.Scroll(0.0f, Y, -1, "/", 1));
}

TEST(QmChatCommandHud, SourceRevisionAndSettingsInvalidateCachedCandidates)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	EXPECT_FALSE(Hud.BeginUpdate("/ra", 3, true, 1));
	EXPECT_FALSE(Hud.MatchesSource(2));
	EXPECT_TRUE(Hud.BeginUpdate("/ra", 3, true, 2));
	EXPECT_EQ(Hud.Count(), 0);
	EXPECT_TRUE(Hud.BeginUpdate("/ra", 3, false, 2));
	Hud.AddServerCommand("rank", "", "Show rank");
	EXPECT_EQ(Hud.Count(), 0);
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::NONE);
}
