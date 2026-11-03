#include <game/client/components/qmclient/mobile_back.h>
#include <game/client/components/qmclient/mobile_spectator_controls.h>
#include <game/client/components/qmclient/mobile_touch_rules.h>
#include <game/client/components/qmclient/mobile_vote_bar.h>

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
	const auto Fingers = FingerAt(Geometry.m_aButtons[NEAREST_TEE].Center());
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 1U << NEAREST_TEE);
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 0U);
	Touch.Update({}, Geometry, Screen);
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 1U << NEAREST_TEE);
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

TEST(QmMobileSpectator, EntityLayerButtonCyclesBackgroundOverlayAndGameTiles)
{
	// 0 = 只看地图背景，100 = 实体层铺满，50 = 两者叠加；三个档位循环。
	EXPECT_EQ(NextEntityLayerValue(0), 50);
	EXPECT_EQ(NextEntityLayerValue(50), 100);
	EXPECT_EQ(NextEntityLayerValue(100), 0);
	// 配置文件里被设成其它数值时也收敛回这三档，不会卡在中间值上。
	EXPECT_EQ(NextEntityLayerValue(30), 100);
	EXPECT_EQ(NextEntityLayerValue(-5), 50);
}

TEST(QmMobileSpectator, DragPanKeepsWorldUnderTheFinger)
{
	// 位移按“当前缩放下的可见世界尺寸”换算：手指划过整屏，画面正好走一整屏的世界距离。
	EXPECT_EQ(DragPan(vec2(0.1f, 0.0f), vec2(1000.0f, 500.0f)), vec2(-100.0f, 0.0f));
	EXPECT_EQ(DragPan(vec2(0.0f, 0.2f), vec2(1000.0f, 500.0f)), vec2(0.0f, -100.0f));
	// 拉远（世界尺寸变大）时同样的手指位移自动走更多世界距离，仍然是 1:1 跟手。
	EXPECT_EQ(DragPan(vec2(0.5f, 0.0f), vec2(2000.0f, 1000.0f)), vec2(-1000.0f, 0.0f));
	EXPECT_EQ(DragPan(vec2(0.1f, 0.0f), vec2(0.0f, 500.0f)), vec2(0.0f, 0.0f));
}

TEST(QmMobileSpectator, DragGestureOnlyPansWhenItStartsOnEmptyArea)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);

	// 空白处按下，这一帧没有位移。
	auto Fingers = FingerAt(Screen * 0.5f);
	Touch.Update(Fingers, Geometry, Screen);
	EXPECT_EQ(Touch.PanDelta(), vec2(0.0f, 0.0f));

	// 同一根手指继续移动，位移按帧累加。
	Fingers[0].m_Delta = vec2(0.1f, -0.05f);
	Touch.Update(Fingers, Geometry, Screen);
	EXPECT_NEAR(Touch.PanDelta().x, 0.1f, 0.00001f);
	EXPECT_NEAR(Touch.PanDelta().y, -0.05f, 0.00001f);

	// 松手后不再保留上一位移，避免画面继续漂移。
	Touch.Update({}, Geometry, Screen);
	EXPECT_EQ(Touch.PanDelta(), vec2(0.0f, 0.0f));
}

TEST(QmMobileSpectator, ButtonPressNeverPansTheCamera)
{
	CTouchState Touch;
	const auto Geometry = Layout(Screen);
	auto Fingers = FingerAt(Geometry.m_aButtons[MENU].Center());
	EXPECT_EQ(Touch.Update(Fingers, Geometry, Screen), 1U << MENU);
	Fingers[0].m_Delta = vec2(0.2f, 0.2f);
	Touch.Update(Fingers, Geometry, Screen);
	EXPECT_EQ(Touch.PanDelta(), vec2(0.0f, 0.0f));
}

TEST(QmMobileSpectator, DragModeHandsTheJoystickAreaOverToPanning)
{
	// 拖动模式下摇杆不再绘制，也不能继续吞掉左下的拖动起手。
	// Layout 第二参是 Demo（决定暂停键），第三参才是摇杆是否参与命中。
	const auto DragLayout = Layout(Screen, true, false);
	EXPECT_FALSE(InsideControl(DragLayout, DragLayout.m_Joystick.Center()));

	CTouchState Touch;
	auto Fingers = FingerAt(DragLayout.m_Joystick.Center());
	Touch.Update(Fingers, DragLayout, Screen);
	EXPECT_EQ(Touch.JoystickDirection(), vec2(0.0f, 0.0f));
	Fingers[0].m_Delta = vec2(-0.05f, 0.0f);
	Touch.Update(Fingers, DragLayout, Screen);
	EXPECT_NEAR(Touch.PanDelta().x, -0.05f, 0.00001f);
}

TEST(QmMobileSpectator, NearestTeePicksClosestVisiblePlayerAndSkipsSelf)
{
	SQmNearestTeeCandidate aCandidates[5];
	aCandidates[0] = {3, true, false, vec2(1000.0f, 0.0f)};
	aCandidates[1] = {4, true, false, vec2(120.0f, 10.0f)};
	aCandidates[2] = {5, true, true, vec2(0.0f, 0.0f)}; // 自己：永远不选
	aCandidates[3] = {6, false, false, vec2(5.0f, 5.0f)}; // 不活跃
	aCandidates[4] = {7, true, false, vec2(400.0f, 400.0f)};

	// 视野中心在原点附近时选最近的活跃玩家。
	EXPECT_EQ(QmNearestTeeClientId(aCandidates, 5, vec2(0.0f, 0.0f)), 4);
	// 中心移到大坐标一侧就换成那个更近的。
	EXPECT_EQ(QmNearestTeeClientId(aCandidates, 5, vec2(900.0f, 0.0f)), 3);
	// 没有可用候选时返回 -1，由调用方给出提示，不能乱切目标。
	EXPECT_EQ(QmNearestTeeClientId(aCandidates, 0, vec2(0.0f, 0.0f)), -1);
	const SQmNearestTeeCandidate aOnlySelf[1] = {{5, true, true, vec2(0.0f, 0.0f)}};
	EXPECT_EQ(QmNearestTeeClientId(aOnlySelf, 1, vec2(0.0f, 0.0f)), -1);
}

TEST(QmMobileSpectator, SpectatorButtonsStayOnScreenAndApart)
{
	const auto Geometry = Layout(Screen);
	for(int i = 0; i < NUM_ACTIONS; ++i)
	{
		const CUIRect &Rect = Geometry.m_aButtons[i];
		SCOPED_TRACE(i);
		EXPECT_GE(Rect.x, 0.0f);
		EXPECT_LE(Rect.x + Rect.w, Screen.x);
		EXPECT_GE(Rect.y, 0.0f);
		EXPECT_LE(Rect.y + Rect.h, Screen.y);
		for(int j = i + 1; j < NUM_ACTIONS; ++j)
		{
			const CUIRect &Other = Geometry.m_aButtons[j];
			const bool Overlap = Rect.x < Other.x + Other.w && Other.x < Rect.x + Rect.w &&
					     Rect.y < Other.y + Other.h && Other.y < Rect.y + Rect.h;
			EXPECT_FALSE(Overlap) << i << " overlaps " << j;
		}
	}
	EXPECT_TRUE(InsideControl(Geometry, Geometry.m_aButtons[SCOREBOARD].Center()));
	EXPECT_TRUE(InsideControl(Geometry, Geometry.m_aButtons[ENTITY_LAYER].Center()));
	EXPECT_TRUE(InsideControl(Geometry, Geometry.m_aButtons[CAMERA_MODE].Center()));
	EXPECT_TRUE(InsideControl(Geometry, Geometry.m_Island.Center()));
	// 灵动岛在底部居中，不能压到右下按钮网格，也不能吃掉摇杆。
	EXPECT_GE(Geometry.m_Island.x, 0.0f);
	EXPECT_LE(Geometry.m_Island.x + Geometry.m_Island.w, Screen.x);
	EXPECT_LE(Geometry.m_Island.y + Geometry.m_Island.h, Screen.y);
	auto Overlaps = [](const CUIRect &A, const CUIRect &B) {
		return A.x < B.x + B.w && B.x < A.x + A.w && A.y < B.y + B.h && B.y < A.y + A.h;
	};
	EXPECT_FALSE(Overlaps(Geometry.m_Island, Geometry.m_Joystick));
	for(int i = 0; i < NUM_ACTIONS; ++i)
		EXPECT_FALSE(Overlaps(Geometry.m_Island, Geometry.m_aButtons[i])) << i;
}

TEST(QmMobileVoteBar, LayoutKeepsButtonsInsideTheBannerAndBigEnoughForFingers)
{
	const auto Layout = QmMobileVoteBarLayout(Screen);
	EXPECT_TRUE(Layout.m_Panel.Inside(Layout.m_Yes.Center()));
	EXPECT_TRUE(Layout.m_Panel.Inside(Layout.m_No.Center()));
	EXPECT_TRUE(Layout.m_Panel.Inside(Layout.m_Description.Center()));
	EXPECT_TRUE(Layout.m_Panel.Inside(Layout.m_Bars.Center()));
	EXPECT_GT(Layout.m_Yes.w, 0.0f);
	EXPECT_LT(Layout.m_Yes.x + Layout.m_Yes.w, Layout.m_No.x);
	// 手指要按得中：两个按钮至少 24 单位高（400 高虚拟屏下约 6% 屏高）。
	EXPECT_GE(Layout.m_Yes.h, 24.0f);
	EXPECT_GE(Layout.m_No.h, 24.0f);
	EXPECT_GE(Layout.m_Yes.x, 0.0f);
	EXPECT_LE(Layout.m_Yes.x + Layout.m_Yes.w, Screen.x);
	EXPECT_LE(Layout.m_Panel.x + Layout.m_Panel.w, Screen.x);
	EXPECT_GE(Layout.m_Panel.y, 0.0f);
}

TEST(QmMobileVoteBar, VisibleOnlyWhileVotingOnMobileWithoutFullScreenUi)
{
	EXPECT_TRUE(QmMobileVoteBarVisible(true, true, true, false, false, false));
	EXPECT_FALSE(QmMobileVoteBarVisible(false, true, true, false, false, false)) << "desktop keeps the small HUD";
	EXPECT_FALSE(QmMobileVoteBarVisible(true, false, true, false, false, false)) << "not online";
	EXPECT_FALSE(QmMobileVoteBarVisible(true, true, false, false, false, false)) << "no vote running";
	EXPECT_FALSE(QmMobileVoteBarVisible(true, true, true, true, false, false)) << "menu open";
	EXPECT_FALSE(QmMobileVoteBarVisible(true, true, true, false, true, false)) << "console open";
	EXPECT_FALSE(QmMobileVoteBarVisible(true, true, true, false, false, true)) << "hud editor open";
}

TEST(QmMobileBackStack, ClosesTopLayerFirstAndOpensMenuOnEmptyStack)
{
	using E = EQmMobileBackTarget;
	// 弹层最先关，其次是聊天，然后计分板、旁观列表、菜单。
	EXPECT_EQ(QmMobileBackTarget(true, true, true, true, true, false), E::POPUP);
	EXPECT_EQ(QmMobileBackTarget(false, true, true, true, true, false), E::CHAT);
	EXPECT_EQ(QmMobileBackTarget(false, false, true, true, true, false), E::SCOREBOARD);
	EXPECT_EQ(QmMobileBackTarget(false, false, false, true, true, false), E::SPECTATOR);
	EXPECT_EQ(QmMobileBackTarget(false, false, false, false, true, false), E::MENU_CLOSE);
	EXPECT_EQ(QmMobileBackTarget(false, false, false, false, false, false), E::MENU_OPEN);
	// “连续两次返回 = 退出”：只有菜单里（或空栈）的第二次才升级成退出确认。
	EXPECT_EQ(QmMobileBackTarget(false, false, false, false, true, true), E::QUIT);
	EXPECT_EQ(QmMobileBackTarget(false, false, false, false, false, true), E::QUIT);
	// 有东西可关时，短时间连按仍然是逐层关，不会直接弹退出。
	EXPECT_EQ(QmMobileBackTarget(true, true, true, true, true, true), E::POPUP);
	EXPECT_EQ(QmMobileBackTarget(false, true, true, true, true, true), E::CHAT);
	EXPECT_EQ(QmMobileBackTarget(false, false, true, true, true, true), E::SCOREBOARD);
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

TEST(QmMobileTouchRules, OutsideTapOnlyClosesForShortStillTap)
{
	EXPECT_TRUE(qm_mobile_touch::OutsideTapClosesPanel(0.0f, 80));
	EXPECT_TRUE(qm_mobile_touch::OutsideTapClosesPanel(0.019f, 399));
	// 滑动（三指截图、边缘起手）不关闭
	EXPECT_FALSE(qm_mobile_touch::OutsideTapClosesPanel(0.05f, 100));
	// 长按不关闭
	EXPECT_FALSE(qm_mobile_touch::OutsideTapClosesPanel(0.0f, 400));
	// 恰好到阈值也不算点击
	EXPECT_FALSE(qm_mobile_touch::OutsideTapClosesPanel(0.02f, 100));
}

TEST(QmMobileTouchRules, OutsideTapArmsOnlyForSingleFingerOutsidePanel)
{
	EXPECT_TRUE(qm_mobile_touch::ShouldArmOutsideTap(1, false));
	// 多指手势（双指滚动、三指截图）不记候选
	EXPECT_FALSE(qm_mobile_touch::ShouldArmOutsideTap(2, false));
	EXPECT_FALSE(qm_mobile_touch::ShouldArmOutsideTap(3, false));
	// 面板内的点击是选择玩家，不是关闭
	EXPECT_FALSE(qm_mobile_touch::ShouldArmOutsideTap(1, true));
}

TEST(QmMobileTouchRules, ScalePercentFollowsPointerDistanceAndClamps)
{
	// 起始 100%、距离翻倍 -> 200%
	EXPECT_EQ(qm_mobile_touch::ScaledPercentByDistance(100, 100.0f, 200.0f, 50, 200), 200);
	// 距离减半 -> 50%
	EXPECT_EQ(qm_mobile_touch::ScaledPercentByDistance(100, 100.0f, 50.0f, 50, 200), 50);
	// 超出区间被夹住
	EXPECT_EQ(qm_mobile_touch::ScaledPercentByDistance(150, 100.0f, 1000.0f, 50, 200), 200);
	EXPECT_EQ(qm_mobile_touch::ScaledPercentByDistance(150, 100.0f, 0.0f, 50, 200), 50);
	// 起始距离非法时返回起始值本身
	EXPECT_EQ(qm_mobile_touch::ScaledPercentByDistance(120, 0.0f, 50.0f, 50, 200), 120);
}
