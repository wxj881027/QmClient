#include <game/client/components/qmclient/replay_session.h>

#include <gtest/gtest.h>

using namespace qmclient::replay;

TEST(QmReplaySession, NoSessionIsReportedInactive)
{
	const SReplaySessionStatus Status = QmResolveReplaySessionStatus({});
	EXPECT_EQ(Status.m_Kind, EReplaySessionKind::NONE);
	EXPECT_FALSE(Status.m_Active);
	EXPECT_FALSE(Status.m_Playing);
	EXPECT_EQ(Status.m_Tick, 0);
	EXPECT_EQ(Status.m_EndTick, 0);
	EXPECT_FLOAT_EQ(Status.m_Speed, 1.0f);
}

TEST(QmReplaySession, ManualGhostPlaybackForwardsPlayheadState)
{
	SReplaySessionInputs Inputs;
	Inputs.m_ManualActive = true;
	Inputs.m_ManualPlaying = true;
	Inputs.m_ManualTick = 120;
	Inputs.m_ManualEndTick = 900;
	Inputs.m_ManualSpeed = 0.5f;
	const SReplaySessionStatus Status = QmResolveReplaySessionStatus(Inputs);
	EXPECT_EQ(Status.m_Kind, EReplaySessionKind::GHOST_MANUAL);
	EXPECT_TRUE(Status.m_Active);
	EXPECT_TRUE(Status.m_Playing);
	EXPECT_EQ(Status.m_Tick, 120);
	EXPECT_EQ(Status.m_EndTick, 900);
	EXPECT_FLOAT_EQ(Status.m_Speed, 0.5f);
}

TEST(QmReplaySession, ViewModeTakesPrecedenceOverManualPlayback)
{
	SReplaySessionInputs Inputs;
	Inputs.m_ViewModeActive = true;
	Inputs.m_ManualActive = true;
	Inputs.m_ManualPlaying = true;
	Inputs.m_ManualTick = 30;
	Inputs.m_ManualEndTick = 600;
	const SReplaySessionStatus Status = QmResolveReplaySessionStatus(Inputs);
	EXPECT_EQ(Status.m_Kind, EReplaySessionKind::VIEW_MODE);
	EXPECT_TRUE(Status.m_Active);
	EXPECT_EQ(Status.m_Tick, 30);
	EXPECT_EQ(Status.m_EndTick, 600);
}

TEST(QmReplaySession, ViewModeWithoutPlaybackIsActiveButPaused)
{
	SReplaySessionInputs Inputs;
	Inputs.m_ViewModeActive = true;
	Inputs.m_ManualActive = false;
	const SReplaySessionStatus Status = QmResolveReplaySessionStatus(Inputs);
	EXPECT_EQ(Status.m_Kind, EReplaySessionKind::VIEW_MODE);
	EXPECT_TRUE(Status.m_Active);
	EXPECT_FALSE(Status.m_Playing);
}

TEST(QmReplaySession, PausedManualPlaybackIsStillAnActiveSession)
{
	SReplaySessionInputs Inputs;
	Inputs.m_ManualActive = true;
	Inputs.m_ManualPlaying = false;
	const SReplaySessionStatus Status = QmResolveReplaySessionStatus(Inputs);
	EXPECT_EQ(Status.m_Kind, EReplaySessionKind::GHOST_MANUAL);
	EXPECT_TRUE(Status.m_Active);
	EXPECT_FALSE(Status.m_Playing);
}
