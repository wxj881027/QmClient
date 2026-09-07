#include <game/client/components/qmclient/features/speedrun_timer/qm_speedrun_timer_logic.h>
#include <game/client/components/qmclient/features/speedrun_timer/qm_speedrun_timer.h>

#include <gtest/gtest.h>

TEST(QmSpeedrunTimer, CalculatesConfiguredAndLegacyDurations)
{
	EXPECT_EQ(QmSpeedrunTimerConfiguredDurationMilliseconds(1, 2, 3, 4), 3723004);
	EXPECT_EQ(QmSpeedrunTimerConfiguredDurationMilliseconds(0, 60, 0, 0), 0);
	EXPECT_EQ(QmSpeedrunTimerConfiguredDurationMilliseconds(0, 0, 60, 0), 0);
	EXPECT_EQ(QmSpeedrunTimerConfiguredDurationMilliseconds(0, 0, 0, 1000), 0);
	EXPECT_EQ(QmSpeedrunTimerLegacyDurationMilliseconds(125), 85000);
	EXPECT_EQ(QmSpeedrunTimerLegacyDurationMilliseconds(160), 0);
}

TEST(QmSpeedrunTimer, FormatsRemainingTime)
{
	char aBuffer[64];
	QmSpeedrunTimerFormat(61004, aBuffer, sizeof(aBuffer));
	EXPECT_STREQ(aBuffer, "01:01.004");
	QmSpeedrunTimerFormat(3661004, aBuffer, sizeof(aBuffer));
	EXPECT_STREQ(aBuffer, "01:01:01.004");
}

TEST(QmSpeedrunTimer, HidesUntilRaceStarts)
{
	CQmSpeedrunTimerLogic Timer;
	SQmSpeedrunTimerInput Input;
	Input.m_Enabled = true;
	Input.m_HasLocalCharacter = true;
	Input.m_TickSpeed = 50;
	Input.m_DurationMilliseconds = 10000;
	EXPECT_FALSE(Timer.Update(Input).m_RequestKill);
	EXPECT_FALSE(Timer.State().m_Visible);

	Input.m_RaceStarted = true;
	Input.m_StartTick = 100;
	Input.m_CurrentTick = 100;
	EXPECT_FALSE(Timer.Update(Input).m_RequestKill);
	EXPECT_TRUE(Timer.State().m_Visible);
	EXPECT_EQ(Timer.State().m_RemainingMilliseconds, 10000);
}

TEST(QmSpeedrunTimer, RequestsKillOnlyOnceAndAutoDisables)
{
	CQmSpeedrunTimerLogic Timer;
	SQmSpeedrunTimerInput Input;
	Input.m_Enabled = true;
	Input.m_HasLocalCharacter = true;
	Input.m_RaceStarted = true;
	Input.m_CanRequestKill = true;
	Input.m_CurrentTick = 150;
	Input.m_StartTick = 100;
	Input.m_TickSpeed = 50;
	Input.m_DurationMilliseconds = 1000;
	Input.m_AutoDisable = true;

	const SQmSpeedrunTimerAction First = Timer.Update(Input);
	EXPECT_TRUE(First.m_RequestKill);
	EXPECT_TRUE(First.m_Disable);
	EXPECT_TRUE(Timer.State().m_Expired);

	const SQmSpeedrunTimerAction Second = Timer.Update(Input);
	EXPECT_FALSE(Second.m_RequestKill);
	EXPECT_FALSE(Second.m_Disable);
	EXPECT_TRUE(Timer.State().m_Visible);
}

TEST(QmSpeedrunTimer, KeepsExpiredMessageWhileLocalCharacterIsTemporarilyMissing)
{
	CQmSpeedrunTimerLogic Timer;
	SQmSpeedrunTimerInput Input;
	Input.m_Enabled = true;
	Input.m_HasLocalCharacter = true;
	Input.m_RaceStarted = true;
	Input.m_CanRequestKill = true;
	Input.m_StartTick = 100;
	Input.m_TickSpeed = 50;
	Input.m_DurationMilliseconds = 1000;
	Input.m_CurrentTick = 150;
	Timer.Update(Input);

	Input.m_HasLocalCharacter = false;
	Input.m_RaceStarted = false;
	Input.m_CurrentTick = 199;
	EXPECT_FALSE(Timer.Update(Input).m_RequestKill);
	EXPECT_FALSE(Timer.State().m_Visible);

	Input.m_HasLocalCharacter = true;
	Input.m_CurrentTick = 200;
	EXPECT_FALSE(Timer.Update(Input).m_RequestKill);
	EXPECT_TRUE(Timer.State().m_Visible);
	EXPECT_TRUE(Timer.State().m_Expired);
}

TEST(QmSpeedrunTimer, DoesNotRequestKillOrDisableOffline)
{
	CQmSpeedrunTimerLogic Timer;
	SQmSpeedrunTimerInput Input;
	Input.m_Enabled = true;
	Input.m_HasLocalCharacter = true;
	Input.m_RaceStarted = true;
	Input.m_CanRequestKill = false;
	Input.m_AutoDisable = true;
	Input.m_StartTick = 100;
	Input.m_TickSpeed = 50;
	Input.m_DurationMilliseconds = 1000;
	Input.m_CurrentTick = 150;

	const SQmSpeedrunTimerAction Action = Timer.Update(Input);
	EXPECT_FALSE(Action.m_RequestKill);
	EXPECT_FALSE(Action.m_Disable);
	EXPECT_TRUE(Timer.State().m_Expired);
}

TEST(QmSpeedrunTimer, ExpiresMessageThenResetsOnNewRace)
{
	CQmSpeedrunTimerLogic Timer;
	SQmSpeedrunTimerInput Input;
	Input.m_Enabled = true;
	Input.m_HasLocalCharacter = true;
	Input.m_RaceStarted = true;
	Input.m_CanRequestKill = true;
	Input.m_StartTick = 100;
	Input.m_TickSpeed = 50;
	Input.m_DurationMilliseconds = 1000;
	Input.m_CurrentTick = 150;
	Timer.Update(Input);

	Input.m_RaceStarted = false;
	Input.m_CurrentTick = 150 + 5 * 50 - 1;
	EXPECT_TRUE(Timer.Update(Input).m_RequestKill == false);
	EXPECT_TRUE(Timer.State().m_Visible);

	Input.m_RaceStarted = true;
	Input.m_StartTick = 300;
	Input.m_CurrentTick = 300;
	EXPECT_FALSE(Timer.Update(Input).m_RequestKill);
	EXPECT_FALSE(Timer.State().m_Expired);
	EXPECT_EQ(Timer.State().m_RemainingMilliseconds, 1000);
}

TEST(QmSpeedrunTimer, DisablingFeatureClearsExpiredState)
{
	CQmSpeedrunTimer Feature;
	SQmSpeedrunTimerInput Input;
	Input.m_Enabled = true;
	Input.m_HasLocalCharacter = true;
	Input.m_RaceStarted = true;
	Input.m_CanRequestKill = true;
	Input.m_StartTick = 100;
	Input.m_TickSpeed = 50;
	Input.m_DurationMilliseconds = 1000;
	Input.m_CurrentTick = 150;
	Feature.UpdateModel(true, true);
	Feature.Update(Input);
	EXPECT_TRUE(Feature.State().m_Expired);

	Feature.UpdateModel(false, true);
	Feature.UpdateModel(true, true);
	EXPECT_FALSE(Feature.State().m_Visible);
	EXPECT_FALSE(Feature.State().m_Expired);
}

TEST(QmSpeedrunTimer, UnavailableFeatureCannotEnterEnabledDispatch)
{
	CQmSpeedrunTimer Feature;
	for(const bool ConfigEnabled : {false, true})
	{
		Feature.UpdateModel(ConfigEnabled, false);
		EXPECT_FALSE(Feature.Model().m_Enabled);
		EXPECT_FALSE(Feature.Model().m_Available);
		Feature.UpdateModel(ConfigEnabled, true);
		EXPECT_EQ(Feature.Model().m_Enabled, ConfigEnabled);
		EXPECT_TRUE(Feature.Model().m_Available);
	}
}
