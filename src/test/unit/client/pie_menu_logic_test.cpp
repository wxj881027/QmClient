#include <game/client/components/pie_menu_logic.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	using namespace qm_pie_menu;

	TEST(PieMenuRename, MixedQueueKeepsPairedNamesInOneSector)
	{
		const auto vOptions = ParseRenameQueue("A|B|C,D|E");
		ASSERT_EQ(vOptions.size(), 4u);
		EXPECT_EQ(vOptions[0].m_Label, "A");
		EXPECT_EQ(vOptions[1].m_Label, "B");
		EXPECT_EQ(vOptions[2].m_Label, "C,D");
		EXPECT_EQ(vOptions[3].m_Label, "E");
		const auto apNames = vOptions[2].Names(false);
		EXPECT_STREQ(apNames[0], "C");
		EXPECT_STREQ(apNames[1], "D");
	}

	TEST(PieMenuRename, PairedNamesKeepMainAndDummyOrderWhileControllingDummy)
	{
		const auto vOptions = ParseRenameQueue("C,D");
		ASSERT_EQ(vOptions.size(), 1u);
		const auto apNames = vOptions[0].Names(true);
		EXPECT_STREQ(apNames[0], "C");
		EXPECT_STREQ(apNames[1], "D");
	}

	TEST(PieMenuRename, SingleNameOnlyChangesTheControlledCharacter)
	{
		const auto vOptions = ParseRenameQueue("A|B");
		ASSERT_EQ(vOptions.size(), 2u);
		const auto apMainNames = vOptions[0].Names(false);
		EXPECT_STREQ(apMainNames[0], "A");
		EXPECT_EQ(apMainNames[1], nullptr);
		const auto apDummyNames = vOptions[1].Names(true);
		EXPECT_EQ(apDummyNames[0], nullptr);
		EXPECT_STREQ(apDummyNames[1], "B");
	}

	TEST(PieMenuRename, WhitespaceIsTrimmedWithoutChangingSpacesInsideNames)
	{
		const auto vOptions = ParseRenameQueue(" | A B |　本体 名字　, 分身 名字　 | E ");
		ASSERT_EQ(vOptions.size(), 3u);
		EXPECT_EQ(vOptions[0].m_Name, "A B");
		EXPECT_EQ(vOptions[1].m_Name, "本体 名字");
		EXPECT_EQ(vOptions[1].m_DummyName, "分身 名字");
		EXPECT_EQ(vOptions[2].m_Name, "E");
	}

	TEST(PieMenuRename, IncompletePairsAreSkippedAndLaterEntriesRemainUsable)
	{
		const auto vOptions = ParseRenameQueue("A|C,|,D| , |B,E|F");
		ASSERT_EQ(vOptions.size(), 3u);
		EXPECT_EQ(vOptions[0].m_Name, "A");
		EXPECT_EQ(vOptions[1].m_Name, "B");
		EXPECT_EQ(vOptions[1].m_DummyName, "E");
		EXPECT_EQ(vOptions[2].m_Name, "F");
	}

	TEST(PieMenuRename, EmptyQueueDoesNotOfferRenameActions)
	{
		EXPECT_TRUE(ParseRenameQueue(nullptr).empty());
		EXPECT_TRUE(ParseRenameQueue("").empty());
		EXPECT_TRUE(ParseRenameQueue("|　|| ").empty());
	}

	TEST(PieMenuRename, LongEntryDoesNotCreateExtraSectorsOrLoseItsDummyName)
	{
		const std::string Name(140, 'A');
		const auto vOptions = ParseRenameQueue((Name + ",分身|E").c_str());
		ASSERT_EQ(vOptions.size(), 2u);
		EXPECT_EQ(vOptions[0].m_Name, Name);
		EXPECT_EQ(vOptions[0].m_DummyName, "分身");
		EXPECT_EQ(vOptions[1].m_Name, "E");
	}

	TEST(PieMenuOptions, HiddenOptionsKeepVisibleActionsAndSectorSelectionAligned)
	{
		std::array<bool, OPTION_COUNT> Enabled;
		Enabled.fill(true);
		Enabled[static_cast<size_t>(EOption::JOIN_TEAM)] = false;
		Enabled[static_cast<size_t>(EOption::FOLLOW)] = false;
		std::array<EOption, OPTION_COUNT> Options;
		const int Count = BuildVisibleOptions(Enabled, Options);
		ASSERT_EQ(Count, static_cast<int>(OPTION_COUNT) - 2);
		EXPECT_EQ(Options[6], EOption::INVITE_TEAM);
		EXPECT_EQ(Options[7], EOption::SCORE);
		EXPECT_EQ(Options[8], EOption::COPY_NAME);
		for(int Index = 0; Index < Count; ++Index)
			EXPECT_EQ(SectorAtAngle(-90.0f + (Index + 0.5f) * 360.0f / Count, -90.0f, Count), Index);
	}

	TEST(PieMenuOptions, AngleWrapAndEmptyRingDoNotSelectInvalidActions)
	{
		const int Count = static_cast<int>(OPTION_COUNT);
		EXPECT_EQ(SectorAtAngle(-90.0f, -90.0f, Count), 0);
		EXPECT_EQ(SectorAtAngle(270.0f, -90.0f, Count), 0);
		EXPECT_EQ(SectorAtAngle(-91.0f, -90.0f, Count), Count - 1);
		EXPECT_EQ(SectorAtAngle(0.0f, -90.0f, 0), -1);
		EXPECT_EQ(SectorAtAngle(std::numeric_limits<float>::quiet_NaN(), -90.0f, Count), -1);
	}

	TEST(PieMenuOptions, CopyNameCanBeHiddenWithoutChangingTheOtherActions)
	{
		std::array<bool, OPTION_COUNT> Enabled;
		Enabled.fill(true);
		Enabled[static_cast<size_t>(EOption::COPY_NAME)] = false;
		std::array<EOption, OPTION_COUNT> Options{};
		const int Count = BuildVisibleOptions(Enabled, Options);
		ASSERT_EQ(Count, static_cast<int>(OPTION_COUNT) - 1);
		EXPECT_EQ(Options[0], EOption::FRIEND);
		EXPECT_EQ(Options[Count - 1], EOption::SCORE);
	}

	TEST(PieMenuOptions, EveryActionCanBeTheOnlyVisibleOption)
	{
		for(size_t Index = 0; Index < OPTION_COUNT; ++Index)
		{
			SCOPED_TRACE(Index);
			std::array<bool, OPTION_COUNT> Enabled{};
			Enabled[Index] = true;
			std::array<EOption, OPTION_COUNT> Options{};
			const int Count = BuildVisibleOptions(Enabled, Options);
			ASSERT_EQ(Count, 1);
			EXPECT_EQ(Options[0], static_cast<EOption>(Index));
			for(float Angle : {-90.0f, 0.0f, 90.0f, 180.0f, 270.0f})
				EXPECT_EQ(SectorAtAngle(Angle, -90.0f, Count), 0);
		}
	}

	TEST(PieMenuOptions, AllActionsCanBeHiddenAndRestored)
	{
		std::array<bool, OPTION_COUNT> Enabled;
		Enabled.fill(true);
		std::array<EOption, OPTION_COUNT> Options{};
		ASSERT_EQ(BuildVisibleOptions(Enabled, Options), OPTION_COUNT);
		const auto OriginalOptions = Options;

		Enabled.fill(false);
		const int EmptyCount = BuildVisibleOptions(Enabled, Options);
		EXPECT_EQ(EmptyCount, 0);
		EXPECT_EQ(SectorAtAngle(0.0f, -90.0f, EmptyCount), -1);

		Enabled.fill(true);
		ASSERT_EQ(BuildVisibleOptions(Enabled, Options), OPTION_COUNT);
		EXPECT_EQ(Options, OriginalOptions);
	}

	TEST(PieMenuTargets, ChangedPlayerIdentityDoesNotMatchTheCapturedTarget)
	{
		EXPECT_TRUE(MatchesPlayer("player", "clan", "player", "clan"));
		EXPECT_FALSE(MatchesPlayer("replacement", "clan", "player", "clan"));
		EXPECT_FALSE(MatchesPlayer("player", "new-clan", "player", "clan"));
		EXPECT_TRUE(MatchesPlayer("player", "new-clan", "player", "clan", true));
		EXPECT_FALSE(MatchesPlayer("", "", "", ""));
	}

	TEST(PieMenuCommands, InviteQuotesPlayerNamesAsASingleArgument)
	{
		EXPECT_EQ(QuotedPlayerCommand("/invite", "a b"), "/invite \"a b\"");
		EXPECT_EQ(QuotedPlayerCommand("/invite", "a\"b\\c"), "/invite \"a\\\"b\\\\c\"");
		EXPECT_TRUE(QuotedPlayerCommand("/invite", "").empty());
	}

	TEST(PieMenuTeams, InviteRequiresANormalLocalTeam)
	{
		EXPECT_EQ(TeamActionStatus(EOption::INVITE_TEAM, true, 1, 0, 64), ETeamActionStatus::READY);
		EXPECT_EQ(TeamActionStatus(EOption::INVITE_TEAM, true, 0, 1, 64), ETeamActionStatus::LOCAL_NEEDS_TEAM);
		EXPECT_EQ(TeamActionStatus(EOption::INVITE_TEAM, true, 64, 1, 64), ETeamActionStatus::LOCAL_NEEDS_TEAM);
		EXPECT_EQ(TeamActionStatus(EOption::INVITE_TEAM, false, 1, 0, 64), ETeamActionStatus::UNSUPPORTED);
	}

	TEST(PieMenuTeams, JoinAllowsTeamZeroButRejectsInvalidAndCurrentTeams)
	{
		EXPECT_EQ(TeamActionStatus(EOption::JOIN_TEAM, true, 0, 2, 64), ETeamActionStatus::READY);
		EXPECT_EQ(TeamActionStatus(EOption::JOIN_TEAM, true, 1, 0, 64), ETeamActionStatus::READY);
		EXPECT_EQ(TeamActionStatus(EOption::JOIN_TEAM, true, 1, -1, 64), ETeamActionStatus::TARGET_NEEDS_TEAM);
		EXPECT_EQ(TeamActionStatus(EOption::JOIN_TEAM, true, 1, 64, 64), ETeamActionStatus::TARGET_NEEDS_TEAM);
		EXPECT_EQ(TeamActionStatus(EOption::JOIN_TEAM, true, 2, 2, 64), ETeamActionStatus::ALREADY_TOGETHER);
	}

}

TEST(PieMenuLifecycle, CommitClosesInteractionBeforeReentrantOrRepeatedInput)
{
	qm_pie_menu::CMenuLifecycle Menu;
	Menu.Open();
	ASSERT_TRUE(Menu.IsInteractive());
	ASSERT_TRUE(Menu.BeginCommit());
	EXPECT_TRUE(Menu.IsVisible());
	EXPECT_FALSE(Menu.IsInteractive());
	EXPECT_FALSE(Menu.BeginCommit());
	Menu.FinishOpening();
	EXPECT_FALSE(Menu.IsInteractive());
}

TEST(PieMenuLifecycle, CancelAndReopenStartFreshInteraction)
{
	qm_pie_menu::CMenuLifecycle Menu;
	EXPECT_FALSE(Menu.BeginCommit());
	Menu.Open();
	Menu.Cancel();
	EXPECT_FALSE(Menu.IsVisible());
	EXPECT_FALSE(Menu.BeginCommit());
	Menu.Open();
	Menu.FinishOpening();
	ASSERT_TRUE(Menu.BeginCommit());
	Menu.Open();
	EXPECT_TRUE(Menu.IsInteractive());
	EXPECT_TRUE(Menu.BeginCommit());
	Menu.Cancel();
	Menu.Cancel();
	EXPECT_FALSE(Menu.IsVisible());
}

TEST(PieMenuLifecycle, ClosingBlocksActionsButKeepsReopenBindingAvailable)
{
	qm_pie_menu::CMenuLifecycle Menu;
	using EInputKind = qm_pie_menu::CMenuLifecycle::EInputKind;
	Menu.Open();
	ASSERT_TRUE(Menu.BeginCommit());
	EXPECT_TRUE(Menu.CapturesClosingInput(EInputKind::ACTION));
	EXPECT_TRUE(Menu.CapturesClosingInput(EInputKind::CANCEL));
	EXPECT_FALSE(Menu.CapturesClosingInput(EInputKind::OTHER));
	Menu.Open();
	EXPECT_TRUE(Menu.IsInteractive());
	EXPECT_FALSE(Menu.CapturesClosingInput(EInputKind::OTHER));
	Menu.Cancel();
	EXPECT_FALSE(Menu.CapturesClosingInput(EInputKind::ACTION));
}
