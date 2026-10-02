#include <game/client/components/qmclient/online_replay_player.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	class CReplaySource : public COnlineReplayPlayer::ISource
	{
	public:
		IDemoPlayer::CInfo m_Info{};
		bool m_Active = true;
		int m_SeekCount = 0;
		CReplaySource()
		{
			m_Info.m_FirstTick = 100;
			m_Info.m_LastTick = 600;
			m_Info.m_CurrentTick = 350;
			m_Info.m_Speed = 1.0f;
			m_Info.m_NumTimelineMarkers = 2;
			m_Info.m_aTimelineMarkers[0] = 200;
			m_Info.m_aTimelineMarkers[1] = 500;
		}
		IDemoPlayer::CInfo PlaybackInfo() const override { return m_Info; }
		bool PlaybackActive() const override { return m_Active; }
		int PlaybackTickSpeed() const override { return 50; }
		void PlaybackSeek(int Tick) override
		{
			++m_SeekCount;
			m_Info.m_CurrentTick = Tick;
		}
		void PlaybackSetPlaying(bool Playing) override { m_Info.m_Paused = !Playing; }
		void PlaybackSetSpeed(float Speed) override { m_Info.m_Speed = Speed; }
		const char *PlaybackFilename() const override { return "rank1/demos/team-run.demo"; }
		IDemoPlayer *PlaybackMetadataReader() const override { return nullptr; }
	};
}

TEST(OnlineReplayPlayer, PercentSeekUsesSourceTickOrigin)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	ASSERT_TRUE(Player.SeekPercent(0.5f));
	EXPECT_EQ(Source.m_Info.m_CurrentTick, 350);
	ASSERT_TRUE(Player.SeekPercent(0.0f));
	EXPECT_EQ(Source.m_Info.m_CurrentTick, 100);
	ASSERT_TRUE(Player.SeekPercent(1.0f));
	EXPECT_EQ(Source.m_Info.m_CurrentTick, 600);
}

TEST(OnlineReplayPlayer, TimeSeekClampsBothEndsAndRejectsNonFiniteInput)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	ASSERT_TRUE(Player.SeekTime(-100.0f));
	EXPECT_EQ(Source.m_Info.m_CurrentTick, 100);
	ASSERT_TRUE(Player.SeekTime(100.0f));
	EXPECT_EQ(Source.m_Info.m_CurrentTick, 600);
	EXPECT_FALSE(Player.SeekTime(std::numeric_limits<float>::infinity()));
	EXPECT_FALSE(Player.SeekPercent(std::numeric_limits<float>::quiet_NaN()));
	EXPECT_FALSE(Player.SeekPercent(-0.1f));
	EXPECT_EQ(Source.m_SeekCount, 2);
}

TEST(OnlineReplayPlayer, TickSeekMovesOneTickAndPreservesPause)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	Player.Pause();
	ASSERT_TRUE(Player.SeekTick(IDemoPlayer::TICK_NEXT));
	EXPECT_EQ(Player.BaseInfo()->m_CurrentTick, 351);
	EXPECT_TRUE(Player.BaseInfo()->m_Paused);
	ASSERT_TRUE(Player.SeekTick(IDemoPlayer::TICK_PREVIOUS));
	EXPECT_EQ(Player.BaseInfo()->m_CurrentTick, 350);
	ASSERT_TRUE(Player.SeekTick(IDemoPlayer::TICK_CURRENT));
	EXPECT_EQ(Player.BaseInfo()->m_CurrentTick, 350);
}

TEST(OnlineReplayPlayer, SharedSpeedStepsIncludeIntermediateAndHighRates)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	Player.AdjustSpeedIndex(1);
	EXPECT_FLOAT_EQ(Source.m_Info.m_Speed, 1.25f);
	Player.AdjustSpeedIndex(-1);
	EXPECT_FLOAT_EQ(Source.m_Info.m_Speed, 1.0f);
	Player.SetSpeedIndex(999);
	EXPECT_FLOAT_EQ(Source.m_Info.m_Speed, 64.0f);
	Player.AdjustSpeedIndex(std::numeric_limits<int>::min());
	EXPECT_FLOAT_EQ(Source.m_Info.m_Speed, 0.1f);
	Player.SetSpeed(std::numeric_limits<float>::quiet_NaN());
	EXPECT_FLOAT_EQ(Source.m_Info.m_Speed, 0.1f);
}

TEST(OnlineReplayPlayer, InactiveAndZeroLengthTimelineDoNotEscapeBounds)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	Source.m_Active = false;
	EXPECT_FALSE(Player.SeekPercent(0.5f));
	EXPECT_EQ(Source.m_SeekCount, 0);
	Source.m_Active = true;
	Source.m_Info.m_LastTick = Source.m_Info.m_FirstTick;
	ASSERT_TRUE(Player.SeekPercent(0.5f));
	EXPECT_EQ(Player.BaseInfo()->m_CurrentTick, 100);
}

TEST(OnlineReplayPlayer, MetadataAndPauseReadTheSourceWithoutChangingSelection)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	EXPECT_EQ(Player.BaseInfo()->m_NumTimelineMarkers, 2);
	EXPECT_EQ(Player.BaseInfo()->m_aTimelineMarkers[1], 500);
	Player.Pause();
	EXPECT_TRUE(Player.BaseInfo()->m_Paused);
	Player.Unpause();
	EXPECT_FALSE(Player.BaseInfo()->m_Paused);
	EXPECT_EQ(Source.m_SeekCount, 0);
	char aName[64];
	Player.GetDemoName(aName, sizeof(aName));
	EXPECT_STREQ(aName, "team-run");
}

TEST(OnlineReplayUi, DoubleEscapeOpensMenuAndRestoresPreviousPanel)
{
	COnlineReplayUiState Ui;
	EXPECT_EQ(Ui.PressEscape(1.0f), COnlineReplayUiState::EAction::TOGGLE_PANEL);
	EXPECT_TRUE(Ui.PanelOpen());
	Ui.ReleaseEscape();
	EXPECT_EQ(Ui.PressEscape(1.2f), COnlineReplayUiState::EAction::OPEN_MENU);
	EXPECT_FALSE(Ui.PanelOpen());
}

TEST(OnlineReplayUi, DoubleEscapeFromOpenPanelKeepsItOpenAfterMenu)
{
	COnlineReplayUiState Ui;
	Ui.PressEscape(1.0f);
	Ui.ReleaseEscape();
	Ui.PressEscape(2.0f);
	EXPECT_FALSE(Ui.PanelOpen());
	Ui.ReleaseEscape();
	EXPECT_EQ(Ui.PressEscape(2.2f), COnlineReplayUiState::EAction::OPEN_MENU);
	EXPECT_TRUE(Ui.PanelOpen());
}

TEST(OnlineReplayUi, HeldEscapeDoesNotTriggerDoubleTap)
{
	COnlineReplayUiState Ui;
	Ui.PressEscape(1.0f);
	EXPECT_EQ(Ui.PressEscape(1.1f), COnlineReplayUiState::EAction::NONE);
	EXPECT_TRUE(Ui.PanelOpen());
	Ui.ReleaseEscape();
	EXPECT_EQ(Ui.PressEscape(2.0f), COnlineReplayUiState::EAction::TOGGLE_PANEL);
	EXPECT_FALSE(Ui.PanelOpen());
}

TEST(OnlineReplayUi, InputOwnerChangeAndSessionResetCancelDoubleTap)
{
	COnlineReplayUiState Ui;
	Ui.PressEscape(1.0f);
	Ui.ReleaseEscape();
	Ui.CancelEscape();
	EXPECT_EQ(Ui.PressEscape(1.1f), COnlineReplayUiState::EAction::TOGGLE_PANEL);
	Ui.Reset();
	EXPECT_FALSE(Ui.PanelOpen());
	EXPECT_EQ(Ui.PressEscape(1.2f), COnlineReplayUiState::EAction::TOGGLE_PANEL);
}

TEST(OnlineReplayPlayer, ExistingInfoPointerReflectsControlChanges)
{
	CReplaySource Source;
	COnlineReplayPlayer Player(Source);
	const auto *pInfo = Player.BaseInfo();
	Player.Pause();
	EXPECT_TRUE(pInfo->m_Paused);
	Player.SetSpeed(2.0f);
	EXPECT_FLOAT_EQ(pInfo->m_Speed, 2.0f);
	Player.SetPos(450);
	EXPECT_EQ(pInfo->m_CurrentTick, 450);
}

TEST(OnlineReplayClock, PauseResumePreservesFractionAndSeekOrigin)
{
	COnlineReplayClock Clock;
	Clock.Start(10.0, 1000, 50);
	Clock.Seek(200, 10.0);
	Clock.SetPlaying(false, 10.125);
	EXPECT_EQ(Clock.Tick(20.0), 206);
	EXPECT_FLOAT_EQ(Clock.Intra(20.0), 0.25f);
	Clock.SetPlaying(true, 20.0);
	EXPECT_EQ(Clock.Tick(20.015), 207);
	EXPECT_NEAR(Clock.Intra(20.015), 0.0f, 0.00001f);
}

TEST(OnlineReplayClock, SpeedChangeKeepsCurrentFrameAndFraction)
{
	COnlineReplayClock Clock;
	Clock.Start(10.0, 1000, 50);
	Clock.Seek(200, 10.0);
	Clock.SetSpeed(2.0f, 10.125);
	EXPECT_DOUBLE_EQ(Clock.Position(10.125), 206.25);
	EXPECT_DOUBLE_EQ(Clock.Position(10.25), 218.75);
	Clock.SetPlaying(false, 10.25);
	Clock.SetSpeed(64.0f, 11.0);
	EXPECT_DOUBLE_EQ(Clock.Position(20.0), 218.75);
}

TEST(OnlineReplayClock, EndClampsAndSeekDoesNotUnpause)
{
	COnlineReplayClock Clock;
	Clock.Start(0.0, 100, 50);
	EXPECT_EQ(Clock.Tick(10.0), 100);
	Clock.Seek(100, 10.0);
	EXPECT_FALSE(Clock.Playing());
	Clock.Seek(0, 11.0);
	EXPECT_FALSE(Clock.Playing());
	Clock.SetPlaying(true, 11.0);
	EXPECT_EQ(Clock.Tick(11.5), 25);
	Clock.Start(12.0, 0, 0);
	EXPECT_FALSE(Clock.Playing());
	EXPECT_EQ(Clock.Tick(100.0), 0);
}

TEST(OnlineReplayMembers, LastSelectedMemberCannotBeDeselected)
{
	COnlineReplayMembers Members;
	Members.Reset(2);
	EXPECT_TRUE(Members.Toggle(0));
	EXPECT_FALSE(Members.Toggle(1));
	EXPECT_TRUE(Members.Selected(1));
	EXPECT_FALSE(Members.Toggle(256));
}

TEST(OnlineReplayMembers, RemovingSelectedMemberRestoresUsableSelection)
{
	COnlineReplayMembers Members;
	Members.Reset(3);
	Members.Toggle(0);
	Members.Toggle(1);
	Members.Remove(2);
	EXPECT_TRUE(Members.Selected(0));
	EXPECT_FALSE(Members.Selected(1));
	Members.Remove(0);
	EXPECT_TRUE(Members.Selected(0));
	Members.Remove(0);
	EXPECT_FALSE(Members.Selected(0));
	Members.Reset(256);
	EXPECT_TRUE(Members.Selected(255));
}

TEST(OnlineReplayUi, InputOwnerChangeAfterPressDoesNotLatchEscape)
{
	COnlineReplayUiState Ui;
	Ui.PressEscape(1.0f);
	Ui.CancelEscape();
	EXPECT_EQ(Ui.PressEscape(2.0f), COnlineReplayUiState::EAction::TOGGLE_PANEL);
}

TEST(OnlineReplayClock, ReenterStartsNewTimelineWithoutOldPauseOrSpeed)
{
	COnlineReplayClock Clock;
	Clock.Start(0.0, 1000, 50);
	Clock.Seek(500, 1.0);
	Clock.SetSpeed(64.0f, 1.0);
	Clock.SetPlaying(false, 1.0);
	Clock.Start(10.0, 200, 50);
	EXPECT_TRUE(Clock.Playing());
	EXPECT_FLOAT_EQ(Clock.Speed(), 1.0f);
	EXPECT_EQ(Clock.Tick(10.5), 25);
}

TEST(OnlineReplaySamples, FrameStepUsesSparseSamplesAndClampsEnds)
{
	const std::vector<int> Ticks{0, 2, 5, 5, 12};
	EXPECT_EQ(OnlineReplayAdjacentTick(Ticks, 2, IDemoPlayer::TICK_NEXT), 5);
	EXPECT_EQ(OnlineReplayAdjacentTick(Ticks, 5, IDemoPlayer::TICK_PREVIOUS), 2);
	EXPECT_EQ(OnlineReplayAdjacentTick(Ticks, 12, IDemoPlayer::TICK_NEXT), 12);
	EXPECT_EQ(OnlineReplayAdjacentTick(Ticks, 0, IDemoPlayer::TICK_PREVIOUS), 0);
	EXPECT_EQ(OnlineReplayAdjacentTick(Ticks, 4, IDemoPlayer::TICK_CURRENT), 4);
	EXPECT_EQ(OnlineReplayAdjacentTick({}, 4, IDemoPlayer::TICK_NEXT), 4);
}

TEST(OnlineReplayShortcuts, SpectatorCommandsMatchOnlyTopLevelCommands)
{
	EXPECT_TRUE(OnlineReplayHasSpectatorBind("spectate_next"));
	EXPECT_TRUE(OnlineReplayHasSpectatorBind("  spectate_previous ; echo done"));
	EXPECT_TRUE(OnlineReplayHasSpectatorBind("spectate 3"));
	EXPECT_TRUE(OnlineReplayHasSpectatorBind("spectate_closest"));
	EXPECT_TRUE(OnlineReplayHasSpectatorBind("+spectate"));
	EXPECT_FALSE(OnlineReplayHasSpectatorBind("say \"spectate_next; spectate 3\""));
	EXPECT_FALSE(OnlineReplayHasSpectatorBind("bind right \"spectate_next\""));
	EXPECT_FALSE(OnlineReplayHasSpectatorBind("spectate_next_extra"));
	EXPECT_FALSE(OnlineReplayHasSpectatorBind(nullptr));
}

TEST(OnlineReplayShortcuts, CameraChangeDoesNotReleaseTimelineClaimUntilNextFrame)
{
	COnlineReplayShortcutClaims Claims;
	Claims.Claim(12, KEY_RIGHT);
	EXPECT_TRUE(Claims.Claimed(12, KEY_RIGHT));
	EXPECT_FALSE(Claims.Claimed(12, KEY_LEFT));
	EXPECT_FALSE(Claims.Claimed(13, KEY_RIGHT));
	Claims.Claim(13, KEY_LEFT);
	Claims.Claim(13, KEY_SPACE);
	EXPECT_TRUE(Claims.Claimed(13, KEY_LEFT));
	EXPECT_TRUE(Claims.Claimed(13, KEY_SPACE));
	EXPECT_FALSE(Claims.Claimed(13, KEY_RIGHT));
	Claims.Claim(13, KEY_LAST);
	EXPECT_FALSE(Claims.Claimed(13, KEY_LAST));
}
