#include <game/client/components/pie_menu_logic.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	using namespace qm_pie_menu;

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

	TEST(PieMenuFollowRefresh, SlowRefreshLeavesTimeToConsumeTheCompletedList)
	{
		double NextRefresh = 0.0;
		EXPECT_TRUE(FollowRefreshDue(false, 0.0, 5, NextRefresh));
		EXPECT_FALSE(FollowRefreshDue(true, 6.0, 5, NextRefresh));
		EXPECT_FALSE(FollowRefreshDue(false, 7.0, 5, NextRefresh));
		EXPECT_TRUE(FollowRefreshDue(false, 11.0, 5, NextRefresh));
	}

	TEST(PieMenuFollowRefresh, ZeroIntervalStillLimitsRefreshRate)
	{
		double NextRefresh = 0.0;
		EXPECT_TRUE(FollowRefreshDue(false, 0.0, 0, NextRefresh));
		EXPECT_FALSE(FollowRefreshDue(false, 4.0, 0, NextRefresh));
		EXPECT_TRUE(FollowRefreshDue(false, 5.0, 0, NextRefresh));
	}

	class CPieMenuFollow : public testing::Test
	{
	protected:
		SFollowState m_State;
		std::string m_Connect;

		void SetUp() override { StartFollow(m_State, "target", "clan"); }
		bool Step(const char *pTargetAddress, const char *pCurrentAddress, double Now, int Delay = 3, bool Connecting = false)
		{
			return FollowStep(m_State, pTargetAddress != nullptr, pTargetAddress, pCurrentAddress, Connecting, Now, Delay, m_Connect);
		}
	};

	TEST_F(CPieMenuFollow, SwitchWaitsForTheConfiguredDelayAndHasNoJumpLimit)
	{
		std::string Current = "server-a";
		for(int Jump = 0; Jump < 5; ++Jump)
		{
			const std::string Target = "server-" + std::to_string(Jump);
			const double Now = 10.0 * Jump;
			EXPECT_FALSE(Step(Target.c_str(), Current.c_str(), Now));
			EXPECT_FALSE(Step(Target.c_str(), Current.c_str(), Now + 2.0));
			ASSERT_TRUE(Step(Target.c_str(), Current.c_str(), Now + 3.0));
			EXPECT_EQ(m_Connect, Target);
			Current = Target;
			EXPECT_FALSE(Step(Target.c_str(), Current.c_str(), Now + 4.0));
			EXPECT_TRUE(m_State.m_Active);
		}
	}

	TEST_F(CPieMenuFollow, OfflineTargetKeepsFollowingAndRestartsTheDelayOnReturn)
	{
		EXPECT_FALSE(Step("server-b", "server-a", 0.0));
		EXPECT_FALSE(Step(nullptr, "server-a", 1.0));
		EXPECT_TRUE(m_State.m_Active);
		EXPECT_TRUE(m_State.m_PendingAddress.empty());
		EXPECT_FALSE(Step("server-b", "server-a", 10.0));
		EXPECT_FALSE(Step("server-b", "server-a", 12.0));
		EXPECT_TRUE(Step("server-b", "server-a", 13.0));
	}

	TEST_F(CPieMenuFollow, ReturnToTheCurrentServerCancelsThePendingJump)
	{
		EXPECT_FALSE(Step("server-b", "server-a", 0.0));
		EXPECT_FALSE(Step("server-a", "server-a", 1.0));
		EXPECT_FALSE(Step("server-a", "server-a", 10.0));
		EXPECT_TRUE(m_State.m_Active);
		EXPECT_TRUE(m_Connect.empty());
	}

	TEST_F(CPieMenuFollow, NewDestinationRestartsTheDelay)
	{
		EXPECT_FALSE(Step("server-b", "server-a", 0.0));
		EXPECT_FALSE(Step("server-c", "server-a", 2.0));
		EXPECT_FALSE(Step("server-c", "server-a", 3.0));
		EXPECT_TRUE(Step("server-c", "server-a", 5.0));
		EXPECT_EQ(m_Connect, "server-c");
	}

	TEST_F(CPieMenuFollow, FailedConnectionRetriesWithoutInterruptingAnInFlightConnection)
	{
		ASSERT_TRUE(Step("server-b", "", 0.0, 0));
		EXPECT_FALSE(Step("server-b", "", 5.0, 0, true));
		EXPECT_TRUE(Step("server-b", "", 6.0, 0));
		EXPECT_FALSE(Step("server-b", "", 7.0, 0));
		EXPECT_TRUE(Step("server-b", "", 11.0, 0));
		EXPECT_FALSE(Step("server-b", "server-b", 12.0, 0));
		EXPECT_TRUE(m_State.m_Active);
	}

	TEST_F(CPieMenuFollow, ManualCancellationPreventsLaterConnections)
	{
		EXPECT_FALSE(Step("server-b", "server-a", 0.0));
		StopFollow(m_State);
		EXPECT_FALSE(Step("server-b", "server-a", 10.0));
		EXPECT_FALSE(m_State.m_Active);
		EXPECT_TRUE(m_State.m_Name.empty());
	}

	TEST_F(CPieMenuFollow, RetargetingDropsThePreviousPendingConnection)
	{
		EXPECT_FALSE(Step("server-b", "server-a", 0.0));
		StartFollow(m_State, "another", "");
		EXPECT_EQ(m_State.m_Name, "another");
		EXPECT_TRUE(m_State.m_PendingAddress.empty());
		EXPECT_FALSE(Step("server-c", "server-a", 4.0));
		EXPECT_TRUE(Step("server-c", "server-a", 7.0));
		EXPECT_EQ(m_Connect, "server-c");
	}
}
