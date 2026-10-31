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
		Hud.SetLayout(10.0f, 250.0f, 200.0f, 200.0f, 8.0f, 0.0f, 500.0f);
	}

	void AddEmojiCandidates(CQmChatCommandHud &Hud, const char *pInput, size_t Cursor)
	{
		Hud.BeginUpdate(pInput, Cursor, true, 1, true);
		Hud.AddEmoji(":ax");
		Hud.AddEmoji(":bx");
		Hud.AddEmoji(":zc");
		Hud.EndUpdate();
		Hud.SetLayout(10.0f, 250.0f, 200.0f, 200.0f, 8.0f, 0.0f, 500.0f);
	}

	float SetSingleRowLayout(CQmChatCommandHud &Hud)
	{
		const float AvailableHeight = Hud.Layout().m_HeaderHeight + Hud.Layout().m_RowHeight + 0.1f;
		Hud.SetLayout(10.0f, 100.0f, 200.0f, AvailableHeight, 8.0f, 0.0f, 500.0f);
		return AvailableHeight;
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
	EXPECT_EQ(Hud.Candidate(1).m_Params, "?r[player]");
	EXPECT_EQ(Hud.Candidate(1).m_Detail, "Show rank");
	EXPECT_EQ(Hud.Candidate(1).m_Label, "/rank ?r[player]");
	EXPECT_TRUE(Hud.Candidate(1).m_HasParams);
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
	const float AvailableHeight = SetSingleRowLayout(Hud);
	ASSERT_EQ(Hud.Layout().m_VisibleRows, 1);
	EXPECT_FLOAT_EQ(Hud.Layout().m_W, 120.0f);
	EXPECT_GE(Hud.Layout().m_Y, 100.0f - AvailableHeight);
	Hud.SetInputTransform(30.0f, -10.0f, 2.0f);
	EXPECT_EQ(Hud.HoveredRow(70.0f, RowCenterY(Hud, 0) * 2.0f - 10.0f), 0);
	EXPECT_FALSE(Hud.Contains(291.0f, RowCenterY(Hud, 0) * 2.0f - 10.0f));
	EXPECT_FALSE(Hud.Contains(20.0f, RowCenterY(Hud, 0)));
	Hud.SetLayout(10.0f, 100.0f, 200.0f, AvailableHeight - 0.2f, 8.0f, 0.0f, 500.0f);
	EXPECT_EQ(Hud.Layout().m_VisibleRows, 0);
}

TEST(QmChatCommandHud, ScrollingKeepsAllCandidatesReachableAndCancelsPressedRow)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/", 1);
	SetSingleRowLayout(Hud);
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

TEST(QmChatCommandHud, MovingCaretMovesPanelAndItsClickTarget)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	Hud.SetLayout(160.0f, 210.0f, 200.0f, 200.0f, 8.0f, 4.0f, 496.0f);
	EXPECT_FLOAT_EQ(Hud.Layout().m_X, 160.0f);
	EXPECT_FLOAT_EQ(Hud.Layout().m_Y + Hud.Layout().m_H, 210.0f);
	const float Y = RowCenterY(Hud, 1);
	EXPECT_FALSE(Hud.Contains(20.0f, Y));
	ASSERT_TRUE(Hud.Press(170.0f, Y, "/ra", 3));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.Release(170.0f, Y, "/ra", 3, Completion, Cursor));
	EXPECT_EQ(Completion, "/rank ");
}

TEST(QmChatCommandHud, ScreenEdgesConstrainPanelWithoutChangingCandidateSelection)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	Hud.SetLayout(490.0f, 210.0f, 200.0f, 200.0f, 8.0f, 4.0f, 496.0f);
	EXPECT_FLOAT_EQ(Hud.Layout().m_X + Hud.Layout().m_W, 496.0f);
	EXPECT_EQ(Hud.HoveredRow(Hud.Layout().m_X + 5.0f, RowCenterY(Hud, 1)), 1);

	Hud.SetLayout(-20.0f, 210.0f, 200.0f, 200.0f, 8.0f, 4.0f, 84.0f);
	EXPECT_FLOAT_EQ(Hud.Layout().m_X, 4.0f);
	EXPECT_FLOAT_EQ(Hud.Layout().m_W, 80.0f);
	EXPECT_EQ(Hud.HoveredRow(20.0f, RowCenterY(Hud, 0)), 0);
}

TEST(QmChatCommandHud, FirstDownSelectsFirstCandidateAndFirstUpSelectsLast)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/", 1);
	EXPECT_EQ(Hud.Selected(), -1);
	ASSERT_TRUE(Hud.MoveSelection(1, "/", 1));
	EXPECT_EQ(Hud.Selected(), 0);
	Hud.Hide();
	AddServerCandidates(Hud, "/", 1);
	ASSERT_TRUE(Hud.MoveSelection(-1, "/", 1));
	EXPECT_EQ(Hud.Selected(), Hud.Count() - 1);
}

TEST(QmChatCommandHud, RecalledCommandLeavesBothDirectionsToHistory)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/r", 2);
	ASSERT_EQ(Hud.Count(), 2);

	EXPECT_FALSE(Hud.MoveSelection(-1, "/r", 2, true));
	EXPECT_FALSE(Hud.MoveSelection(-1, "/r", 2, true));
	EXPECT_FALSE(Hud.MoveSelection(1, "/r", 2, true));
	EXPECT_EQ(Hud.Selected(), -1);
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/r", 2, Completion, Cursor));
}

TEST(QmChatCommandHud, LeavingHistoryRestoresCandidateNavigationForSameText)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/r", 2);
	EXPECT_FALSE(Hud.MoveSelection(-1, "/r", 2, true));

	// 草稿可以与最后一条历史相同，退出历史后无需改变文本即可恢复候选导航。
	ASSERT_TRUE(Hud.MoveSelection(1, "/r", 2, false));
	EXPECT_EQ(Hud.Selected(), 0);
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.CompleteSelected("/r", 2, Completion, Cursor));
	EXPECT_EQ(Completion, "/race");
}

TEST(QmChatCommandHud, RecalledEmojiLeavesBothDirectionsToHistory)
{
	CQmChatCommandHud Hud;
	AddEmojiCandidates(Hud, ":", 1);
	ASSERT_EQ(Hud.Count(), 3);

	EXPECT_FALSE(Hud.MoveSelection(-1, ":", 1, true));
	EXPECT_FALSE(Hud.MoveSelection(1, ":", 1, true));
	EXPECT_EQ(Hud.Selected(), -1);
}

TEST(QmChatCommandHud, RecalledCommandStillAllowsExplicitMouseCompletion)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/r", 2);
	EXPECT_FALSE(Hud.MoveSelection(-1, "/r", 2, true));

	const float Y = RowCenterY(Hud, 0);
	ASSERT_TRUE(Hud.Press(20.0f, Y, "/r", 2));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.Release(20.0f, Y, "/r", 2, Completion, Cursor));
	EXPECT_EQ(Completion, "/race");
}

TEST(QmChatCommandHud, DirectionSelectionWrapsAtBothEnds)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/", 1);
	ASSERT_TRUE(Hud.MoveSelection(1, "/", 1));
	ASSERT_TRUE(Hud.MoveSelection(-1, "/", 1));
	EXPECT_EQ(Hud.Selected(), 2);
	ASSERT_TRUE(Hud.MoveSelection(1, "/", 1));
	EXPECT_EQ(Hud.Selected(), 0);
}

TEST(QmChatCommandHud, SelectionBeforeFirstLayoutBecomesVisibleWhenLayoutIsAvailable)
{
	CQmChatCommandHud Hud;
	ASSERT_TRUE(Hud.BeginUpdate("/", 1, true, 1));
	Hud.AddServerCommand("rank", "?r[player]", "Show rank");
	Hud.AddServerCommand("race", "", "Show time");
	Hud.AddServerCommand("team", "?i[team]", "Join a team");
	Hud.EndUpdate();
	EXPECT_EQ(Hud.Layout().m_VisibleRows, 0);
	ASSERT_TRUE(Hud.MoveSelection(-1, "/", 1));
	EXPECT_EQ(Hud.Selected(), 2);
	Hud.SetLayout(10.0f, 100.0f, 200.0f, 30.0f, 8.0f, 0.0f, 500.0f);
	ASSERT_EQ(Hud.Layout().m_VisibleRows, 1);
	EXPECT_EQ(Hud.Offset(), 2);
	EXPECT_EQ(Hud.HoveredRow(20.0f, RowCenterY(Hud, 0)), Hud.Selected());
}

TEST(QmChatCommandHud, ClickingOutsidePanelCancelsKeyboardSelection)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(1, "/ra", 3));
	EXPECT_FALSE(Hud.Press(-10.0f, RowCenterY(Hud, 0), "/ra", 3));
	EXPECT_EQ(Hud.Selected(), -1);
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
}

TEST(QmChatCommandHud, KeyboardSelectionScrollsIntoViewAndCancelsPressedRow)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/", 1);
	SetSingleRowLayout(Hud);
	ASSERT_TRUE(Hud.Press(20.0f, RowCenterY(Hud, 0), "/", 1));
	ASSERT_TRUE(Hud.MoveSelection(-1, "/", 1));
	EXPECT_EQ(Hud.Selected(), 2);
	EXPECT_EQ(Hud.Offset(), 2);
	EXPECT_FALSE(Hud.IsPressed());
	ASSERT_TRUE(Hud.MoveSelection(1, "/", 1));
	EXPECT_EQ(Hud.Selected(), 0);
	EXPECT_EQ(Hud.Offset(), 0);
}

TEST(QmChatCommandHud, CompletionRequiresExplicitKeyboardSelection)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
	ASSERT_TRUE(Hud.MoveSelection(-1, "/ra", 3));
	ASSERT_TRUE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
	EXPECT_EQ(Completion, "/rank ");
	EXPECT_EQ(Cursor, Completion.size());
	EXPECT_EQ(Hud.Selected(), -1);
	EXPECT_FALSE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
}

TEST(QmChatCommandHud, KeyboardCompletionPreservesExistingArguments)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra \"Player name\"", 3);
	ASSERT_TRUE(Hud.MoveSelection(-1, "/ra \"Player name\"", 3));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.CompleteSelected("/ra \"Player name\"", 3, Completion, Cursor));
	EXPECT_EQ(Completion, "/rank \"Player name\"");
	EXPECT_EQ(Cursor, 5U);
}

TEST(QmChatCommandHud, CompletingNoParameterCommandLeavesNoSpaceAndAllowsNextConfirmationToSend)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(1, "/ra", 3));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
	EXPECT_EQ(Completion, "/race");
	EXPECT_EQ(Cursor, 5U);
	AddServerCandidates(Hud, "/race", 5);
	EXPECT_EQ(Hud.Selected(), -1);
	EXPECT_FALSE(Hud.CompleteSelected("/race", 5, Completion, Cursor));
}

TEST(QmChatCommandHud, EditingInputOrMovingCursorRejectsStaleKeyboardSelection)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(1, "/ra", 3));
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/rac", 4, Completion, Cursor));
	EXPECT_FALSE(Hud.CompleteSelected("/ra", 2, Completion, Cursor));
	EXPECT_FALSE(Hud.MoveSelection(1, "/rac", 4));
	EXPECT_FALSE(Hud.MoveSelection(1, "/ra", 2));
}

TEST(QmChatCommandHud, UpdatingInputClearsSelectionBeforeRebuildingCandidates)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(-1, "/ra", 3));
	AddServerCandidates(Hud, "/rac", 4);
	EXPECT_EQ(Hud.Selected(), -1);
	ASSERT_EQ(Hud.Count(), 1);
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/rac", 4, Completion, Cursor));
}

TEST(QmChatCommandHud, HidingCancelsKeyboardSelectionAndRejectsFurtherNavigation)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(1, "/ra", 3));
	Hud.Hide();
	EXPECT_EQ(Hud.Selected(), -1);
	EXPECT_FALSE(Hud.MoveSelection(1, "/ra", 3));
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
}

TEST(QmChatCommandHud, SourceRevisionChangeClearsKeyboardSelection)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(1, "/ra", 3));
	ASSERT_TRUE(Hud.BeginUpdate("/ra", 3, true, 2));
	Hud.AddServerCommand("rank", "?r[player]", "Show rank");
	Hud.EndUpdate();
	Hud.SetLayout(10.0f, 250.0f, 200.0f, 200.0f, 8.0f, 0.0f, 500.0f);
	EXPECT_EQ(Hud.Selected(), -1);
	std::string Completion;
	size_t Cursor = 0;
	EXPECT_FALSE(Hud.CompleteSelected("/ra", 3, Completion, Cursor));
}

TEST(QmChatCommandHud, UnchangedInputKeepsKeyboardSelectionAcrossRefresh)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(-1, "/ra", 3));
	EXPECT_FALSE(Hud.BeginUpdate("/ra", 3, true, 1));
	EXPECT_EQ(Hud.Selected(), 1);
}

TEST(QmChatCommandHud, DisablingCommandsClearsSelectionAndLeavesNavigationAlone)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/ra", 3);
	ASSERT_TRUE(Hud.MoveSelection(1, "/ra", 3));
	ASSERT_TRUE(Hud.BeginUpdate("/ra", 3, false, 1));
	Hud.AddServerCommand("rank", "", "Show rank");
	Hud.EndUpdate();
	EXPECT_EQ(Hud.Selected(), -1);
	EXPECT_FALSE(Hud.MoveSelection(1, "/ra", 3));
}

TEST(QmChatCommandHud, NoMatchingCommandLeavesNavigationAlone)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/missing", 8);
	EXPECT_EQ(Hud.Count(), 0);
	EXPECT_FALSE(Hud.MoveSelection(-1, "/missing", 8));
}

TEST(QmChatCommandHud, EmojiPrefixMatchesWithoutDependingOnCommandSetting)
{
	CQmChatCommandHud Hud;
	ASSERT_TRUE(Hud.BeginUpdate(":A", 2, false, 1, true));
	Hud.AddEmoji(":ax");
	Hud.AddEmoji(":bx");
	Hud.EndUpdate();
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::EMOJI);
	ASSERT_EQ(Hud.Count(), 1);
	EXPECT_EQ(Hud.Candidate(0).m_Name, ":ax");
	EXPECT_TRUE(Hud.Candidate(0).m_Params.empty());
	EXPECT_TRUE(Hud.Candidate(0).m_Detail.empty());
	EXPECT_FALSE(Hud.Candidate(0).m_HasParams);
}

TEST(QmChatCommandHud, EmojiKeyboardCompletionPreservesTextAroundCurrentToken)
{
	CQmChatCommandHud Hud;
	AddEmojiCandidates(Hud, "hello :a tail", 8);
	ASSERT_EQ(Hud.Count(), 1);
	ASSERT_TRUE(Hud.MoveSelection(1, "hello :a tail", 8));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.CompleteSelected("hello :a tail", 8, Completion, Cursor));
	EXPECT_EQ(Completion, "hello :ax tail");
	EXPECT_EQ(Cursor, 9U);
}

TEST(QmChatCommandHud, EmojiMouseCompletionPreservesFullwidthColon)
{
	const char *pInput = "hello ：A tail";
	const size_t InputCursor = str_length("hello ：A");
	CQmChatCommandHud Hud;
	AddEmojiCandidates(Hud, pInput, InputCursor);
	ASSERT_EQ(Hud.Count(), 1);
	ASSERT_TRUE(Hud.Press(20.0f, RowCenterY(Hud, 0), pInput, InputCursor));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.Release(20.0f, RowCenterY(Hud, 0), pInput, InputCursor, Completion, Cursor));
	EXPECT_EQ(Completion, "hello ：ax tail");
	EXPECT_EQ(Cursor, (size_t)str_length("hello ：ax"));
}

TEST(QmChatCommandHud, EmojiSettingChangeClearsSelectionAndDisablesCandidates)
{
	CQmChatCommandHud Hud;
	AddEmojiCandidates(Hud, ":", 1);
	ASSERT_TRUE(Hud.MoveSelection(1, ":", 1));
	ASSERT_TRUE(Hud.BeginUpdate(":", 1, true, 1, false));
	Hud.AddEmoji(":ax");
	Hud.EndUpdate();
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::NONE);
	EXPECT_EQ(Hud.Count(), 0);
	EXPECT_EQ(Hud.Selected(), -1);
	EXPECT_FALSE(Hud.MoveSelection(1, ":", 1));
}

TEST(QmChatCommandHud, EmojiRowsUseLessHeightThanCommandDescriptionRows)
{
	CQmChatCommandHud CommandHud;
	AddServerCandidates(CommandHud, "/", 1);
	CQmChatCommandHud EmojiHud;
	AddEmojiCandidates(EmojiHud, ":", 1);
	ASSERT_GT(EmojiHud.Layout().m_VisibleRows, 0);
	EXPECT_LT(EmojiHud.Layout().m_RowHeight, CommandHud.Layout().m_RowHeight);
}

TEST(QmChatCommandHud, CommandArgumentCursorDoesNotSelectCommandButCanCompleteEmojiToken)
{
	CQmChatCommandHud Hud;
	AddServerCandidates(Hud, "/w player :a", 12);
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::NONE);
	EXPECT_FALSE(Hud.MoveSelection(1, "/w player :a", 12));
	AddEmojiCandidates(Hud, "/w player :a", 12);
	EXPECT_EQ(Hud.Source(), CQmChatCommandHud::ESource::EMOJI);
	ASSERT_TRUE(Hud.MoveSelection(1, "/w player :a", 12));
	std::string Completion;
	size_t Cursor = 0;
	ASSERT_TRUE(Hud.CompleteSelected("/w player :a", 12, Completion, Cursor));
	EXPECT_EQ(Completion, "/w player :ax");
	EXPECT_EQ(Cursor, 13U);
}
