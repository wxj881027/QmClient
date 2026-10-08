#include <game/client/components/qmclient/dummy_hammer_input.h>
#include <game/gamecore.h>

#include <gtest/gtest.h>

namespace
{
	class CQmDummyHammerInputTest : public testing::Test
	{
	protected:
		bool Snap(bool Force = false, bool RestoreWeapon = true, vec2 Direction = vec2(40.0f, -20.0f))
		{
			return m_State.SnapInput(m_Output, m_BaseInput, Direction, RestoreWeapon, Force);
		}

		CQmDummyHammerInput m_State;
		CNetObj_PlayerInput m_BaseInput{};
		CNetObj_PlayerInput m_Output{};
	};
}

TEST_F(CQmDummyHammerInputTest, ClickReleasedBeforeSamplingStillEmitsOneReleasedHammer)
{
	m_State.SetEnabled(true);
	m_State.SetEnabled(false);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 1);
	EXPECT_EQ(m_Output.m_Fire & 1, 0);
	EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(m_Output.m_TargetX, 40);
	EXPECT_EQ(m_Output.m_TargetY, -20);
}

TEST_F(CQmDummyHammerInputTest, MultipleClicksBeforeSamplingRetainTheirFireTransitions)
{
	for(int i = 0; i < 3; ++i)
	{
		m_State.SetEnabled(true);
		m_State.SetEnabled(false);
	}
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 3);
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Releases, 3);
}

TEST_F(CQmDummyHammerInputTest, ReleaseAndRepressBetweenSamplesStartsANewHammer)
{
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	const int FirstFire = m_Output.m_Fire;
	m_State.SetEnabled(false);
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(FirstFire, m_Output.m_Fire).m_Presses, 1);
	EXPECT_EQ(m_Output.m_Fire & 1, 1);
}

TEST_F(CQmDummyHammerInputTest, RepeatedEnableDoesNotCreateExtraPresses)
{
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	const int FirstFire = m_Output.m_Fire;
	for(int i = 0; i < 6; ++i)
	{
		m_State.SetEnabled(true);
		if(Snap())
			EXPECT_EQ(m_Output.m_Fire, FirstFire);
	}
}

TEST_F(CQmDummyHammerInputTest, HeldHammerKeepsTheExistingTwentyFiveSampleInterval)
{
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	const int FirstFire = m_Output.m_Fire;
	for(int i = 1; i < 25; ++i)
	{
		if(Snap())
			EXPECT_EQ(m_Output.m_Fire, FirstFire);
	}
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(FirstFire, m_Output.m_Fire).m_Presses, 1);
}

TEST_F(CQmDummyHammerInputTest, HeldHammerResendsTheSameCounterAndAimTwice)
{
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	const CNetObj_PlayerInput First = m_Output;
	for(int i = 0; i < 2; ++i)
	{
		ASSERT_TRUE(Snap(false, true, vec2(-90.0f, 60.0f)));
		EXPECT_EQ(m_Output.m_Fire, First.m_Fire);
		EXPECT_EQ(m_Output.m_WantedWeapon, First.m_WantedWeapon);
		EXPECT_EQ(m_Output.m_TargetX, First.m_TargetX);
		EXPECT_EQ(m_Output.m_TargetY, First.m_TargetY);
	}
	EXPECT_FALSE(Snap(true));
}

TEST_F(CQmDummyHammerInputTest, ReleaseBeforeResendsKeepsHammerWeaponAndAimWithoutHoldingFire)
{
	m_BaseInput.m_WantedWeapon = WEAPON_GUN + 1;
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	const int FirstFire = m_Output.m_Fire;
	m_State.SetEnabled(false);
	for(int i = 0; i < 2; ++i)
	{
		ASSERT_TRUE(Snap(false, true, vec2(-90.0f, 60.0f)));
		EXPECT_EQ(m_Output.m_Fire & 1, 0);
		EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 1);
		EXPECT_EQ(CountInput(FirstFire, m_Output.m_Fire).m_Presses, 0);
		EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_HAMMER + 1);
		EXPECT_EQ(m_Output.m_TargetX, 40);
		EXPECT_EQ(m_Output.m_TargetY, -20);
	}
	ASSERT_TRUE(Snap());
	EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_GUN + 1);
	EXPECT_EQ(m_Output.m_Fire & 1, 0);
	EXPECT_FALSE(Snap());
}

TEST_F(CQmDummyHammerInputTest, ShortClickFinishesResendsBeforeRestoringThePreviousWeapon)
{
	m_BaseInput.m_WantedWeapon = WEAPON_LASER + 1;
	m_State.SetEnabled(true);
	m_State.SetEnabled(false);
	ASSERT_TRUE(Snap());
	const int Fire = m_Output.m_Fire;
	for(int i = 0; i < 2; ++i)
	{
		ASSERT_TRUE(Snap());
		EXPECT_EQ(m_Output.m_Fire, Fire);
		EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_HAMMER + 1);
	}
	ASSERT_TRUE(Snap());
	EXPECT_EQ(m_Output.m_Fire, Fire);
	EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_LASER + 1);
}

TEST_F(CQmDummyHammerInputTest, ReleaseAfterResendsStillEmitsAReleaseWithoutForce)
{
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	ASSERT_TRUE(Snap());
	ASSERT_TRUE(Snap());
	EXPECT_FALSE(Snap());
	const int FirstFire = m_Output.m_Fire;
	m_State.SetEnabled(false);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(FirstFire, m_Output.m_Fire).m_Releases, 1);
	EXPECT_EQ(CountInput(FirstFire, m_Output.m_Fire).m_Presses, 0);
	ASSERT_TRUE(Snap());
	EXPECT_FALSE(Snap());
}

TEST_F(CQmDummyHammerInputTest, DisablingWeaponRestoreLeavesTheDummyOnHammer)
{
	m_BaseInput.m_WantedWeapon = WEAPON_GUN + 1;
	m_State.SetEnabled(true);
	m_State.SetEnabled(false);
	for(int i = 0; i < 4; ++i)
		ASSERT_TRUE(Snap(false, false));
	EXPECT_EQ(m_Output.m_WantedWeapon, WEAPON_HAMMER + 1);
	EXPECT_EQ(m_BaseInput.m_WantedWeapon, WEAPON_HAMMER + 1);
}

TEST_F(CQmDummyHammerInputTest, ManualOddFireCounterCannotSuppressTheNextHammer)
{
	m_State.ObserveManualInput(1);
	m_BaseInput.m_Fire = 1;
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(1, m_Output.m_Fire).m_Presses, 1);
	EXPECT_NE(m_Output.m_Fire, 1);
}

TEST_F(CQmDummyHammerInputTest, FireCounterWrapsWithinTheProtocolMask)
{
	m_State.ObserveManualInput(INPUT_STATE_MASK);
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_GE(m_Output.m_Fire, 0);
	EXPECT_LE(m_Output.m_Fire, INPUT_STATE_MASK);
	EXPECT_EQ(CountInput(INPUT_STATE_MASK, m_Output.m_Fire).m_Presses, 1);
}

TEST_F(CQmDummyHammerInputTest, ManualInputSeedsThePredictionMirrorBeforeTheNextHammerSample)
{
	m_State.ObserveManualInput(37);
	EXPECT_EQ(m_State.HammerInput().m_Fire, 37);
	m_State.SetEnabled(true);
	EXPECT_EQ(m_State.HammerInput().m_Fire, 37);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(37, m_State.HammerInput().m_Fire).m_Presses, 1);
}

TEST(QmDummyHammerInput, ConnectionCountersRemainIndependentWhenControlChanges)
{
	CQmDummyHammerInput aStates[2];
	CNetObj_PlayerInput Base{};
	CNetObj_PlayerInput Output{};
	aStates[0].ObserveManualInput(11);
	aStates[1].ObserveManualInput(37);
	aStates[1].SetEnabled(true);
	ASSERT_TRUE(aStates[1].SnapInput(Output, Base, vec2(20.0f, 10.0f), true, false));
	EXPECT_EQ(CountInput(37, Output.m_Fire).m_Presses, 1);
	aStates[1].ObserveManualInput(Output.m_Fire);
	aStates[0].SetEnabled(true);
	ASSERT_TRUE(aStates[0].SnapInput(Output, Base, vec2(-20.0f, -10.0f), true, false));
	EXPECT_EQ(CountInput(11, Output.m_Fire).m_Presses, 1);
}

TEST_F(CQmDummyHammerInputTest, ManualControlCancelsQueuedClicksAndResends)
{
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	m_State.SetEnabled(false);
	m_State.SetEnabled(true);
	m_State.ObserveManualInput(20);
	m_BaseInput.m_Fire = 20;
	EXPECT_FALSE(Snap());
	ASSERT_TRUE(Snap(true));
	EXPECT_EQ(m_Output.m_Fire, 20);
	EXPECT_EQ(m_Output.m_WantedWeapon, 0);
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(20, m_Output.m_Fire).m_Presses, 1);
}

TEST_F(CQmDummyHammerInputTest, ResetDropsPendingClicksAndStartsWithANewConnectionCounter)
{
	m_State.SetEnabled(true);
	m_State.SetEnabled(false);
	m_State.Reset();
	EXPECT_FALSE(Snap());
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 1);
}

TEST_F(CQmDummyHammerInputTest, DirectConfigurationDisableCancelsAnUnsentRequest)
{
	m_State.SetEnabled(true);
	m_State.SynchronizeEnabled(false);
	EXPECT_FALSE(Snap());
	m_State.SynchronizeEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(0, m_Output.m_Fire).m_Presses, 1);
}

TEST_F(CQmDummyHammerInputTest, NormalDummyInputKeepsItsExistingForceAndMovementRules)
{
	EXPECT_FALSE(Snap());
	m_BaseInput.m_Fire = 7;
	ASSERT_TRUE(Snap(true));
	EXPECT_EQ(m_Output.m_Fire, 7);
	m_BaseInput.m_Direction = -1;
	ASSERT_TRUE(Snap());
	EXPECT_EQ(m_Output.m_Direction, -1);
	m_State.SetEnabled(true);
	ASSERT_TRUE(Snap());
	EXPECT_EQ(CountInput(7, m_Output.m_Fire).m_Presses, 1);
}
