#include <game/client/components/qmclient/mobile_spectator_controls.h>

#include <gtest/gtest.h>

#include <limits>

using namespace qm_mobile_spectator;

TEST(QmMobileSpectator, JoystickCenterAndDeadzoneDoNotMove)
{
	EXPECT_EQ(Direction(vec2(0.0f, 0.0f), 50.0f), vec2(0.0f, 0.0f));
	EXPECT_EQ(Direction(vec2(5.0f, 0.0f), 50.0f), vec2(0.0f, 0.0f));
}

TEST(QmMobileSpectator, JoystickClampsDiagonalAndOutsideDrags)
{
	EXPECT_NEAR(length(Direction(vec2(100.0f, 100.0f), 50.0f)), 1.0f, 0.00001f);
	EXPECT_EQ(Direction(vec2(-200.0f, 0.0f), 50.0f), vec2(-1.0f, 0.0f));
}

TEST(QmMobileSpectator, CameraMovesAtSameSpeedAcrossFrameRates)
{
	vec2 At30(0.0f, 0.0f), At60(0.0f, 0.0f);
	for(int i = 0; i < 30; ++i)
		At30 += Movement(vec2(1.0f, 0.0f), 1.0f, 1.0f / 30.0f);
	for(int i = 0; i < 60; ++i)
		At60 += Movement(vec2(1.0f, 0.0f), 1.0f, 1.0f / 60.0f);
	EXPECT_NEAR(At30.x, 1000.0f, 0.001f);
	EXPECT_NEAR(At30.x, At60.x, 0.001f);
	EXPECT_EQ(Movement(vec2(1.0f, 0.0f), 2.0f, 0.01f), vec2(20.0f, 0.0f));
}

TEST(QmMobileSpectator, InvalidFrameTimeStopsAndLongFrameIsBounded)
{
	EXPECT_EQ(Movement(vec2(1.0f, 0.0f), 1.0f, -1.0f), vec2(0.0f, 0.0f));
	EXPECT_EQ(Movement(vec2(1.0f, 0.0f), 1.0f, std::numeric_limits<float>::quiet_NaN()), vec2(0.0f, 0.0f));
	EXPECT_EQ(Movement(vec2(1.0f, 0.0f), 1.0f, 10.0f), vec2(50.0f, 0.0f));
}

namespace
{
	const vec2 Screen(720.0f, 400.0f);
	std::vector<IInput::CTouchFingerState> FingerAt(vec2 Position, int Press = 0)
	{
		IInput::CTouchFingerState Finger{};
		Finger.m_Position = Position / Screen;
		Finger.m_PressTime = std::chrono::nanoseconds(Press);
		return {Finger};
	}
}

TEST(QmMobileSpectator, HiddenPauseButtonAreaIsAvailableForOnlineMapTap)
{
	CTouchState Touch;
	const auto DemoGeometry = Layout(Screen);
	const auto OnlineGeometry = Layout(Screen, false);
	const auto Fingers = FingerAt(DemoGeometry.m_aButtons[PAUSE].Center());
	EXPECT_EQ(Touch.Update(Fingers, OnlineGeometry, Screen, 1.0), 0U);
	EXPECT_FALSE(Touch.MapTapped());
	EXPECT_EQ(Touch.Update({}, OnlineGeometry, Screen, 1.1), 0U);
	EXPECT_TRUE(Touch.MapTapped());
}

TEST(QmMobileSpectator, HeldJoystickMovesWithoutFurtherFingerDelta)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const auto Fingers = FingerAt(Geometry.m_Joystick.Center() + vec2(40.0f, 0.0f));
	Touch.Update(Fingers, Geometry, Screen);
	const vec2 FirstDirection = Touch.JoystickDirection();
	Touch.Update(Fingers, Geometry, Screen);
	EXPECT_GT(FirstDirection.x, 0.0f);
	EXPECT_EQ(Touch.JoystickDirection(), FirstDirection);
}

TEST(QmMobileSpectator, ReleaseStopsJoystickImmediately)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	Touch.Update(FingerAt(Geometry.m_Joystick.Center() + vec2(40.0f, 0.0f)), Geometry, Screen);
	Touch.Update({}, Geometry, Screen);
	EXPECT_EQ(Touch.JoystickDirection(), vec2(0.0f, 0.0f));
}

TEST(QmMobileSpectator, CancelRequiresReleaseBeforeJoystickCanRestart)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const auto Fingers = FingerAt(Geometry.m_Joystick.Center() + vec2(40.0f, 0.0f));
	Touch.Update(Fingers, Geometry, Screen);
	Touch.Cancel();
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 0U);
	EXPECT_EQ(Touch.JoystickDirection(), vec2(0.0f, 0.0f));
	Touch.Update({}, Geometry, Screen);
	Touch.Update(Fingers, Geometry, Screen);
	EXPECT_GT(Touch.JoystickDirection().x, 0.0f);
}

TEST(QmMobileSpectator, HeldButtonFiresOnceAndCanBePressedAgain)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const auto Fingers = FingerAt(Geometry.m_aButtons[FREEVIEW].Center());
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 1U << FREEVIEW);
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 0U);
	Touch.Update({}, Geometry, Screen);
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 1U << FREEVIEW);
}

TEST(QmMobileSpectator, ControlsAreExcludedFromMapGestureStarts)
{
	const auto Geometry = Layout(Screen);
	EXPECT_TRUE(InsideControl(Geometry, Geometry.m_Joystick.Center()));
	for(const auto &Button : Geometry.m_aButtons)
		EXPECT_TRUE(InsideControl(Geometry, Button.Center()));
	EXPECT_FALSE(InsideControl(Geometry, Screen * 0.5f));
}

TEST(QmMobileSpectator, MapTapFiresOnlyOnReleaseAndCanBePressedAgain)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const auto Fingers = FingerAt(Screen * 0.5f);
	Touch.Update(Fingers, Geometry, Screen, 1.0);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update(Fingers, Geometry, Screen, 1.1);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update({}, Geometry, Screen, 1.2);
	EXPECT_TRUE(Touch.MapTapped());
	Touch.Update({}, Geometry, Screen, 1.3);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update(Fingers, Geometry, Screen, 2.0);
	Touch.Update({}, Geometry, Screen, 2.1);
	EXPECT_TRUE(Touch.MapTapped());
}

TEST(QmMobileSpectator, SmallMapTapMotionIsAllowed)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const vec2 Start = Screen * 0.5f;
	Touch.Update(FingerAt(Start), Geometry, Screen, 1.0);
	Touch.Update(FingerAt(Start + vec2(3.0f, 4.0f)), Geometry, Screen, 1.1);
	Touch.Update({}, Geometry, Screen, 1.2);
	EXPECT_TRUE(Touch.MapTapped());
}

TEST(QmMobileSpectator, MapDragCannotTurnBackIntoTap)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const vec2 Start = Screen * 0.5f;
	Touch.Update(FingerAt(Start), Geometry, Screen, 1.0);
	Touch.Update(FingerAt(Start + vec2(20.0f, 0.0f)), Geometry, Screen, 1.1);
	Touch.Update(FingerAt(Start), Geometry, Screen, 1.2);
	Touch.Update({}, Geometry, Screen, 1.3);
	EXPECT_FALSE(Touch.MapTapped());
}

TEST(QmMobileSpectator, LongMapPressDoesNotTapEvenWithoutIntermediateUpdates)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	Touch.Update(FingerAt(Screen * 0.5f), Geometry, Screen, 1.0);
	Touch.Update({}, Geometry, Screen, 1.5);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update(FingerAt(Screen * 0.5f), Geometry, Screen, 2.0);
	Touch.Update(FingerAt(Screen * 0.5f), Geometry, Screen, 2.6);
	Touch.Update({}, Geometry, Screen, 2.7);
	EXPECT_FALSE(Touch.MapTapped());
}

TEST(QmMobileSpectator, JoystickAndEveryButtonAreExcludedFromMapTaps)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	Touch.Update(FingerAt(Geometry.m_Joystick.Center()), Geometry, Screen, 1.0);
	Touch.Update({}, Geometry, Screen, 1.1);
	EXPECT_FALSE(Touch.MapTapped());
	for(const auto &Button : Geometry.m_aButtons)
	{
		Touch.Update(FingerAt(Button.Center()), Geometry, Screen, 2.0);
		Touch.Update({}, Geometry, Screen, 2.1);
		EXPECT_FALSE(Touch.MapTapped());
	}
}

TEST(QmMobileSpectator, PinchAndRemainingFingerNeverProduceMapTap)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	auto Fingers = FingerAt(vec2(280.0f, 200.0f));
	Touch.Update(Fingers, Geometry, Screen, 1.0);
	Fingers.push_back(FingerAt(vec2(380.0f, 200.0f), 1).front());
	Touch.Update(Fingers, Geometry, Screen, 1.1);
	Fingers.back().m_Position = vec2(480.0f, 200.0f) / Screen;
	Touch.Update(Fingers, Geometry, Screen, 1.2);
	EXPECT_LT(Touch.ZoomFactor(), 1.0f);
	Fingers.pop_back();
	Touch.Update(Fingers, Geometry, Screen, 1.3);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update({}, Geometry, Screen, 1.4);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update(Fingers, Geometry, Screen, 2.0);
	Touch.Update({}, Geometry, Screen, 2.1);
	EXPECT_TRUE(Touch.MapTapped());
}

TEST(QmMobileSpectator, AdditionalControlFingerCancelsMapTapWithoutPinch)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	auto Fingers = FingerAt(Screen * 0.5f);
	Touch.Update(Fingers, Geometry, Screen, 1.0);
	Fingers.push_back(FingerAt(Geometry.m_Joystick.Center(), 1).front());
	Touch.Update(Fingers, Geometry, Screen, 1.1);
	Fingers.pop_back();
	Touch.Update(Fingers, Geometry, Screen, 1.2);
	Touch.Update({}, Geometry, Screen, 1.3);
	EXPECT_FALSE(Touch.MapTapped());
}

TEST(QmMobileSpectator, ReplacedMapFingerCannotFinishOriginalTap)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	Touch.Update(FingerAt(Screen * 0.5f, 1), Geometry, Screen, 1.0);
	Touch.Update(FingerAt(Screen * 0.5f, 2), Geometry, Screen, 1.1);
	Touch.Update({}, Geometry, Screen, 1.2);
	EXPECT_FALSE(Touch.MapTapped());
}

TEST(QmMobileSpectator, CancelClearsMapTapAndRequiresReleaseBeforeNewTap)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const auto Fingers = FingerAt(Screen * 0.5f);
	Touch.Update(Fingers, Geometry, Screen, 1.0);
	Touch.Cancel();
	Touch.Update(Fingers, Geometry, Screen, 1.1);
	Touch.Update({}, Geometry, Screen, 1.2);
	EXPECT_FALSE(Touch.MapTapped());
	Touch.Update(Fingers, Geometry, Screen, 2.0);
	Touch.Update({}, Geometry, Screen, 2.1);
	EXPECT_TRUE(Touch.MapTapped());
	Touch.Cancel();
	EXPECT_FALSE(Touch.MapTapped());
}

TEST(QmMobileSpectator, FirstPinchFrameOnlyEstablishesDistance)
{
	CPinchState<int> Pinch;
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f)), 1.0f);
	EXPECT_TRUE(Pinch.Active());
}

TEST(QmMobileSpectator, SpreadingFingersMagnifiesAndClosingWidensView)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(200.0f, 0.0f)), 0.5f);
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f)), 2.0f);
}

TEST(QmMobileSpectator, HoldingPinchDistanceDoesNotKeepZooming)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(30.0f, 20.0f), 2, vec2(130.0f, 20.0f)), 1.0f);
}

TEST(QmMobileSpectator, ReorderingSameFingersKeepsPinchDistance)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	EXPECT_FLOAT_EQ(Pinch.Update(2, vec2(200.0f, 0.0f), 1, vec2(0.0f, 0.0f)), 0.5f);
}

TEST(QmMobileSpectator, ReplacingFingerRebasesWithoutZoomJump)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 3, vec2(300.0f, 0.0f)), 1.0f);
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 3, vec2(150.0f, 0.0f)), 2.0f);
}

TEST(QmMobileSpectator, NearCoincidentAndInvalidPositionsResetPinchBaseline)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(0.0f, 0.0f)), 1.0f);
	EXPECT_TRUE(Pinch.Active());
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(200.0f, 0.0f)), 1.0f);
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f)), 1.0f);
	EXPECT_TRUE(Pinch.Active());
}

TEST(QmMobileSpectator, ResetPreventsOldGestureDistanceFromAffectingNewGesture)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	Pinch.Reset();
	EXPECT_FALSE(Pinch.Active());
	EXPECT_FLOAT_EQ(Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(300.0f, 0.0f)), 1.0f);
}

TEST(QmMobileSpectator, PinchScaleDependsOnDistanceInsteadOfFrameCount)
{
	CPinchState<int> Pinch;
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	const float First = Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(150.0f, 0.0f));
	const float Second = Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(200.0f, 0.0f));
	Pinch.Reset();
	Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(100.0f, 0.0f));
	EXPECT_FLOAT_EQ(First * Second, Pinch.Update(1, vec2(0.0f, 0.0f), 2, vec2(200.0f, 0.0f)));
}

TEST(QmMobileSpectator, JoystickDragAcrossButtonDoesNotActivateButton)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	Touch.Update(FingerAt(Geometry.m_Joystick.Center()), Geometry, Screen);
	EXPECT_EQ(Touch.Update(FingerAt(Geometry.m_aButtons[MENU].Center()), Geometry, Screen), 0U);
}

TEST(QmMobileSpectator, PlayerSelectorOpensAfterOpeningFingerIsReleased)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	const auto Fingers = FingerAt(Geometry.m_aButtons[SELECT].Center());
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 0U);
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 0U);
	EXPECT_EQ(Touch.Update({}, Geometry, Screen), 1U << SELECT);
	EXPECT_EQ(Touch.Update({}, Geometry, Screen), 0U);
}

TEST(QmMobileSpectator, InterruptionCancelsPendingPlayerSelector)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	Touch.Update(FingerAt(Geometry.m_aButtons[SELECT].Center()), Geometry, Screen);
	Touch.Cancel();
	EXPECT_EQ(Touch.Update({}, Geometry, Screen), 0U);
}

TEST(QmMobileSpectator, AutoSpectateWaitsForLocalInfoAndThrottlesRetries)
{
	CJoinState Join;
	EXPECT_FALSE(Join.Update(true, false, false, 1.0));
	EXPECT_TRUE(Join.Update(true, true, false, 1.0));
	EXPECT_FALSE(Join.Update(true, true, false, 2.0));
	EXPECT_TRUE(Join.Update(true, true, false, 4.0));
}

TEST(QmMobileSpectator, SpectatorAcknowledgementAndReconnectResetRetryState)
{
	CJoinState Join;
	EXPECT_TRUE(Join.Update(true, true, false, 1.0));
	EXPECT_FALSE(Join.Update(true, true, true, 2.0));
	EXPECT_TRUE(Join.Update(true, true, false, 2.1));
	EXPECT_FALSE(Join.Update(false, false, false, 2.2));
	EXPECT_TRUE(Join.Update(true, true, false, 2.3));
}

TEST(QmMobileSpectator, GameplayInputIsReleasedWithoutLosingCameraAndChatFlags)
{
	CNetObj_PlayerInput Input{};
	Input.m_Direction = 1;
	Input.m_Jump = Input.m_Hook = 1;
	Input.m_Fire = 63;
	Input.m_NextWeapon = Input.m_PrevWeapon = Input.m_WantedWeapon = 2;
	Input.m_TargetX = 123;
	Input.m_TargetY = 456;
	Input.m_PlayerFlags = PLAYERFLAG_CHATTING | PLAYERFLAG_SPEC_CAM;
	NeutralizeGameplayInput(Input);
	EXPECT_EQ(Input.m_Direction, 0);
	EXPECT_EQ(Input.m_Jump, 0);
	EXPECT_EQ(Input.m_Hook, 0);
	EXPECT_EQ(Input.m_Fire, 0);
	EXPECT_EQ(Input.m_NextWeapon, 0);
	EXPECT_EQ(Input.m_PrevWeapon, 0);
	EXPECT_EQ(Input.m_WantedWeapon, 0);
	EXPECT_EQ(Input.m_TargetX, 123);
	EXPECT_EQ(Input.m_TargetY, 456);
	EXPECT_EQ(Input.m_PlayerFlags, PLAYERFLAG_CHATTING | PLAYERFLAG_SPEC_CAM);
	NeutralizeGameplayInput(Input);
	EXPECT_EQ(Input.m_Fire, 0);
}
