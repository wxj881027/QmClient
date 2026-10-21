#include <game/client/components/qmclient/voice/voice_vad_state.h>

#include <gtest/gtest.h>

TEST(VoiceVadState, StartsInactive)
{
	CVoiceVadState State;
	EXPECT_FALSE(State.IsActive());
	State.PrepareUpdate(true, 1000);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, TriggerActivates)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	EXPECT_TRUE(State.IsActive());
}

TEST(VoiceVadState, SilenceDoesNotActivate)
{
	CVoiceVadState State;
	State.UpdateFrame(false, 1000, 500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, PeakMustReachThreshold)
{
	EXPECT_FALSE(CVoiceVadState::ShouldActivate(0.49f, 0.5f));
	EXPECT_TRUE(CVoiceVadState::ShouldActivate(0.5f, 0.5f));
	EXPECT_TRUE(CVoiceVadState::ShouldActivate(0.51f, 0.5f));
	EXPECT_TRUE(CVoiceVadState::ShouldActivate(1.0f, 1.0f));
}

TEST(VoiceVadState, ZeroThresholdIncludesSilentFrames)
{
	CVoiceVadState State;
	State.UpdateFrame(CVoiceVadState::ShouldActivate(0.0f, 0.0f), 1000, 500);
	EXPECT_TRUE(State.IsActive());
}

TEST(VoiceVadState, TriggerExtendsExistingReleaseDelay)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(true, 1400, 500);
	State.UpdateFrame(false, 1500, 500);
	EXPECT_TRUE(State.IsActive());
	State.UpdateFrame(false, 1900, 500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, SilentFramesKeepActiveBeforeDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(false, 1200, 500);
	State.UpdateFrame(false, 1499, 500);
	EXPECT_TRUE(State.IsActive());
}

TEST(VoiceVadState, SilentFrameReleasesAtDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(false, 1500, 500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, SilentFrameReleasesAfterDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(false, 1501, 500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, CanReactivateAfterRelease)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(false, 1500, 500);
	ASSERT_FALSE(State.IsActive());
	State.UpdateFrame(true, 2000, 500);
	EXPECT_TRUE(State.IsActive());
	State.UpdateFrame(false, 2499, 500);
	EXPECT_TRUE(State.IsActive());
	State.UpdateFrame(false, 2500, 500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, NoFrameUpdateKeepsActiveBeforeDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.PrepareUpdate(true, 1499);
	EXPECT_TRUE(State.IsActive());
}

TEST(VoiceVadState, NoFrameUpdateExpiresAtDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.PrepareUpdate(true, 1500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, NoFrameUpdateExpiresAfterDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.PrepareUpdate(true, 1501);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, ZeroDelayReleasesOnFirstSilentFrame)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 0);
	ASSERT_TRUE(State.IsActive());
	State.UpdateFrame(false, 1000, 0);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, ZeroDelayStaysActiveWithoutFrames)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 0);
	State.PrepareUpdate(true, 2000);
	EXPECT_TRUE(State.IsActive());
	State.UpdateFrame(false, 2000, 0);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, ZeroDelayTriggerClearsPriorDeadline)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(true, 1100, 0);
	State.PrepareUpdate(true, 1500);
	EXPECT_TRUE(State.IsActive());
	State.UpdateFrame(false, 1501, 0);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, ReleaseDelayChangeAffectsNextTriggerOnly)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.UpdateFrame(false, 1200, 0);
	EXPECT_TRUE(State.IsActive());
	State.UpdateFrame(false, 1500, 1000);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, PositiveDelayReplacesZeroDelayOnTrigger)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 0);
	State.UpdateFrame(true, 1100, 500);
	State.PrepareUpdate(true, 1599);
	EXPECT_TRUE(State.IsActive());
	State.PrepareUpdate(true, 1600);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, SwitchingToPttClearsVoiceActivation)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.PrepareUpdate(false, 1100);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, SwitchingBackToVadStartsInactive)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 0);
	State.PrepareUpdate(false, 1100);
	State.PrepareUpdate(true, 1200);
	EXPECT_FALSE(State.IsActive());
	State.UpdateFrame(false, 1200, 500);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, ResetClearsActivationAndAllowsFreshTrigger)
{
	CVoiceVadState State;
	State.UpdateFrame(true, 1000, 500);
	State.Reset();
	State.UpdateFrame(false, 1100, 500);
	EXPECT_FALSE(State.IsActive());
	State.UpdateFrame(true, 1200, 500);
	State.PrepareUpdate(true, 1500);
	EXPECT_TRUE(State.IsActive());
	State.PrepareUpdate(true, 1700);
	EXPECT_FALSE(State.IsActive());
}

TEST(VoiceVadState, RepeatedResetKeepsInactive)
{
	CVoiceVadState State;
	State.Reset();
	State.UpdateFrame(true, 1000, 0);
	State.Reset();
	State.Reset();
	State.PrepareUpdate(true, 1100);
	EXPECT_FALSE(State.IsActive());
}
