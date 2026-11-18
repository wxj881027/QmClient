#include <game/client/components/controls.h>

#include <gtest/gtest.h>

TEST(ControlsInputReset, ReleasesHeldHookInPreviousAndCurrentInputOnComponentReset)
{
	CNetObj_PlayerInput LastInput = {};
	LastInput.m_Direction = -1;
	LastInput.m_TargetX = 128;
	LastInput.m_Jump = 1;
	LastInput.m_Fire = 5;
	LastInput.m_Hook = 1;
	CNetObj_PlayerInput Input = LastInput;

	CControls::ResetHeldInput(LastInput, Input, CControls::EHookReset::RELEASE_HELD);

	EXPECT_EQ(LastInput.m_Hook, 0);
	EXPECT_EQ(Input.m_Hook, 0);
	EXPECT_EQ(Input.m_Direction, 0);
	EXPECT_EQ(Input.m_Jump, 0);
	EXPECT_EQ(Input.m_Fire, 6);
	EXPECT_EQ(Input.m_TargetX, 128);
}

TEST(ControlsInputReset, PreservesHeldHookDuringTemporaryReset)
{
	CNetObj_PlayerInput LastInput = {};
	LastInput.m_Jump = 1;
	LastInput.m_Fire = 6;
	LastInput.m_Hook = 1;
	CNetObj_PlayerInput Input = {};

	CControls::ResetHeldInput(LastInput, Input, CControls::EHookReset::KEEP_HELD);

	EXPECT_EQ(LastInput.m_Hook, 1);
	EXPECT_EQ(Input.m_Hook, 1);
	EXPECT_EQ(Input.m_Jump, 0);
	EXPECT_EQ(Input.m_Fire, 6);
}
