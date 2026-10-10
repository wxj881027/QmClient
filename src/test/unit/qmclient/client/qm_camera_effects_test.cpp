#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

TEST(QmCameraEffects, DynamicFovRemovalKeepsBaseZoomStable)
{
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(2.0f, 1.25f), 1.6f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(1.6f, 1.0f), 1.6f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomWithoutDynamicFov(1.6f, 0.0f), 1.6f);
}

TEST(QmCameraEffects, ZoomReverseRetargetKeepsStepsButDropsInertiaOnReversal)
{
	constexpr float ZoomInFactor = 0.866025f;
	constexpr float ZoomOutFactor = 1.154700f;

	// 未在缩放动画中：步进基准就是画面当前值
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(1.0f, 0.5f, ZoomInFactor, false, true, false), 1.0f);
	// 同向按键：基准仍是旧目标，连点 N 下仍是 N 步
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(0.95f, 0.866f, ZoomInFactor, true, true, false), 0.866f);
	// 反向按键：改以画面当前值为基准，本次动画立刻朝新方向运动
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(0.95f, 0.866f, ZoomOutFactor, true, true, false), 0.95f);
	// 关闭开关：完全保留上游"以旧目标为基准"的行为
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(0.95f, 0.866f, ZoomOutFactor, true, false, false), 0.866f);

	// 反向：新目标在速度反方向，继承速度归零
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomDerivativeOnRetarget(0.95f, -0.5f, 1.1f, true), 0.0f);
	// 同向：保留继承速度，维持 C1 连续
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomDerivativeOnRetarget(0.95f, -0.5f, 0.8f, true), -0.5f);
	// 关闭开关：保留上游继承速度（先沿旧方向滑行再掉头）
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomDerivativeOnRetarget(0.95f, -0.5f, 1.1f, false), -0.5f);

	// 曲线层面：上游曲线在反向按键后仍朝旧方向走，修复后的曲线第一帧就朝新目标走
	const float StartZoom = 0.95f;
	const float ReverseTarget = 1.1f;
	const CCubicBezier UpstreamCurve = CCubicBezier::With(StartZoom, -0.5f, 0.0f, ReverseTarget);
	const CCubicBezier RetargetedCurve = CCubicBezier::With(StartZoom, QmCameraEffects::ZoomDerivativeOnRetarget(StartZoom, -0.5f, ReverseTarget, true), 0.0f, ReverseTarget);
	EXPECT_LT(UpstreamCurve.Evaluate(0.05f), StartZoom);
	EXPECT_GT(RetargetedCurve.Evaluate(0.05f), StartZoom);
}

TEST(QmCameraEffects, ZoomOutDuringDefaultResetUsesDefaultTarget)
{
	const float ZoomOutFactor = CCamera::ZoomStepsToValue(-1.0f);

	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(2.0f, 1.0f, ZoomOutFactor, true, true, true), 1.0f);
	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(1.05f, 1.0f, ZoomOutFactor, true, true, true), 1.0f);
}

TEST(QmCameraEffects, ZoomOutDuringResetUsesConfiguredDefaultTarget)
{
	const float DefaultZoom = CCamera::ZoomStepsToValue(-2.0f);
	const float ZoomOutFactor = CCamera::ZoomStepsToValue(-1.0f);

	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(2.0f, DefaultZoom, ZoomOutFactor, true, true, true), DefaultZoom);
}

TEST(QmCameraEffects, CompletedResetUsesCurrentZoomForNextStep)
{
	const float ZoomOutFactor = CCamera::ZoomStepsToValue(-1.0f);

	EXPECT_FLOAT_EQ(QmCameraEffects::ZoomTargetBaseOnRetarget(1.0f, 0.5f, ZoomOutFactor, false, true, true), 1.0f);
}

TEST(QmCameraEffects, CinematicFreeviewSmoothingIsFrameRateIndependent)
{
	const vec2 Start(10.0f, 20.0f);
	const vec2 Target(30.0f, 60.0f);
	vec2 At30Fps = Start;
	vec2 At60Fps = Start;
	for(int Frame = 0; Frame < 30; ++Frame)
		At30Fps = QmCameraEffects::SmoothCinematicPosition(At30Fps, Target, 1.0f / 30.0f);
	for(int Frame = 0; Frame < 60; ++Frame)
		At60Fps = QmCameraEffects::SmoothCinematicPosition(At60Fps, Target, 1.0f / 60.0f);

	EXPECT_FLOAT_EQ(QmCameraEffects::SmoothCinematicPosition(Start, Target, 0.0f).x, Start.x);
	EXPECT_NEAR(At30Fps.x, At60Fps.x, 0.0001f);
	EXPECT_NEAR(At30Fps.y, At60Fps.y, 0.0001f);
	EXPECT_GT(At30Fps.x, Start.x);
	EXPECT_LT(At30Fps.x, Target.x);
}

TEST(QmCameraEffects, SmoothPositionMatchesCinematicWrapperAtLegacyHalfLife)
{
	const vec2 Start(10.0f, 20.0f);
	const vec2 Target(30.0f, 60.0f);
	for(float FrameTime : {1.0f / 144.0f, 1.0f / 60.0f, 1.0f / 30.0f, 0.05f})
	{
		SCOPED_TRACE(::testing::Message() << "frameTime=" << FrameTime);
		EXPECT_EQ(QmCameraEffects::SmoothPosition(Start, Target, FrameTime, 0.09f), QmCameraEffects::SmoothCinematicPosition(Start, Target, FrameTime));
	}
}

TEST(QmCameraEffects, SmoothPositionIsFrameRateIndependentAndConverges)
{
	const vec2 Start(-40.0f, 15.0f);
	const vec2 Target(20.0f, -25.0f);
	vec2 At30Fps = Start;
	vec2 At60Fps = Start;
	vec2 At144Fps = Start;
	for(int Frame = 0; Frame < 30; ++Frame)
		At30Fps = QmCameraEffects::SmoothPosition(At30Fps, Target, 1.0f / 30.0f, 0.12f);
	for(int Frame = 0; Frame < 60; ++Frame)
		At60Fps = QmCameraEffects::SmoothPosition(At60Fps, Target, 1.0f / 60.0f, 0.12f);
	for(int Frame = 0; Frame < 144; ++Frame)
		At144Fps = QmCameraEffects::SmoothPosition(At144Fps, Target, 1.0f / 144.0f, 0.12f);

	EXPECT_NEAR(At30Fps.x, At60Fps.x, 0.0001f);
	EXPECT_NEAR(At30Fps.y, At60Fps.y, 0.0001f);
	EXPECT_NEAR(At60Fps.x, At144Fps.x, 0.0001f);
	EXPECT_GT(At30Fps.x, Start.x);
	EXPECT_LT(At30Fps.x, Target.x);
}

TEST(QmCameraEffects, SmoothPositionRejectsInvalidTimeAndHalfLife)
{
	const vec2 Start(1.0f, 2.0f);
	const vec2 Target(5.0f, 6.0f);
	EXPECT_EQ(QmCameraEffects::SmoothPosition(Start, Target, 0.0f, 0.09f), Start);
	EXPECT_EQ(QmCameraEffects::SmoothPosition(Start, Target, -0.016f, 0.09f), Start);
	EXPECT_EQ(QmCameraEffects::SmoothPosition(Start, Target, std::numeric_limits<float>::infinity(), 0.09f), Start);
	EXPECT_EQ(QmCameraEffects::SmoothPosition(Start, Target, 0.016f, 0.0f), Start);
	EXPECT_EQ(QmCameraEffects::SmoothPosition(Start, Target, 0.016f, -0.09f), Start);
}

namespace
{
	QmCameraEffects::SFreeviewCameraInput FreeviewInput(int State = IClient::STATE_ONLINE)
	{
		QmCameraEffects::SFreeviewCameraInput Input;
		Input.m_ClientState = State;
		Input.m_Spectating = true;
		Input.m_Enabled = true;
		Input.m_DemoTick = 100;
		return Input;
	}
}

TEST(QmFreeviewCamera, LiveAndDemoUseTheSameMouseSmoothing)
{
	QmCameraEffects::CFreeviewCameraSmoothing Live, Demo;
	const auto LiveInput = FreeviewInput();
	const auto DemoInput = FreeviewInput(IClient::STATE_DEMOPLAYBACK);
	vec2 LiveCenter{}, DemoCenter{};
	for(int Frame = 0; Frame < 20; ++Frame)
	{
		LiveCenter = Live.Update(LiveInput, LiveCenter, vec2(100, 50), 1.0f / 60.0f);
		DemoCenter = Demo.Update(DemoInput, DemoCenter, vec2(100, 50), 1.0f / 60.0f);
		EXPECT_EQ(LiveCenter, DemoCenter);
	}
	EXPECT_GT(LiveCenter.x, 0.0f);
	EXPECT_LT(LiveCenter.x, 100.0f);
}

TEST(QmFreeviewCamera, FollowingAndNormalGameplayDoNotSmoothAndClearFreeviewState)
{
	for(int State : {IClient::STATE_ONLINE, IClient::STATE_DEMOPLAYBACK})
	{
		SCOPED_TRACE(State);
		for(int Mode = 0; Mode < 3; ++Mode)
		{
			SCOPED_TRACE(Mode);
			QmCameraEffects::CFreeviewCameraSmoothing Camera;
			auto Input = FreeviewInput(State);
			Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
			if(Mode == 0)
				Input.m_Spectating = false;
			else if(Mode == 1)
				Input.m_UsePosition = true;
			else
				Input.m_SpectatorId = 5;
			EXPECT_EQ(Camera.Update(Input, vec2(50, 0), vec2(200, 0), 0.096f), vec2(200, 0));
			EXPECT_FALSE(Camera.Active());
		}
	}
}

TEST(QmFreeviewCamera, PausingAndResumingReanchorAndPausedMouseStillMoves)
{
	QmCameraEffects::CFreeviewCameraSmoothing Camera;
	auto Input = FreeviewInput(IClient::STATE_DEMOPLAYBACK);
	Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
	Input.m_DemoPaused = true;
	EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
	EXPECT_NEAR(Camera.Update(Input, vec2(300, 0), vec2(500, 0), 0.096f).x, 400.0f, 0.001f);
	Input.m_DemoPaused = false;
	EXPECT_NEAR(Camera.Update(Input, vec2(600, 0), vec2(800, 0), 0.096f).x, 700.0f, 0.001f);
}

TEST(QmFreeviewCamera, SpeedChangeReanchorsWithoutScalingMouseFrameTime)
{
	QmCameraEffects::CFreeviewCameraSmoothing Camera;
	auto Input = FreeviewInput(IClient::STATE_DEMOPLAYBACK);
	Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
	Input.m_DemoSpeed = 4.0f;
	EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
	// 正常倍速前进的 tick 不应被当作跳转，也不应重用外部旧锚点。
	Input.m_DemoTick += 19;
	EXPECT_NEAR(Camera.Update(Input, vec2(0, 0), vec2(500, 0), 0.096f).x, 400.0f, 0.001f);
}

TEST(QmFreeviewCamera, TimelineJumpsReanchorForBothSeekDirections)
{
	for(int Tick : {0, 1000, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		SCOPED_TRACE(Tick);
		QmCameraEffects::CFreeviewCameraSmoothing Camera;
		auto Input = FreeviewInput(IClient::STATE_DEMOPLAYBACK);
		Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
		Input.m_DemoTick = Tick;
		EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
	}
}

TEST(QmFreeviewCamera, PausedSingleTickSeekReanchors)
{
	QmCameraEffects::CFreeviewCameraSmoothing Camera;
	auto Input = FreeviewInput(IClient::STATE_DEMOPLAYBACK);
	Input.m_DemoPaused = true;
	Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
	++Input.m_DemoTick;
	EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
}

TEST(QmFreeviewCamera, PlaybackSourceAndConnectionChangesDoNotReusePreviousAnchor)
{
	QmCameraEffects::CFreeviewCameraSmoothing Camera;
	auto Input = FreeviewInput();
	Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
	Input.m_ClientState = IClient::STATE_DEMOPLAYBACK;
	EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
	Input.m_Connection = 1;
	EXPECT_NEAR(Camera.Update(Input, vec2(600, 0), vec2(800, 0), 0.096f).x, 700.0f, 0.001f);
	Input.m_ClientState = IClient::STATE_OFFLINE;
	EXPECT_EQ(Camera.Update(Input, vec2(), vec2(900, 0), 0.096f), vec2(900, 0));
	EXPECT_FALSE(Camera.Active());
}

TEST(QmFreeviewCamera, DisabledAndZeroSmoothnessSnapThenReenableFromCurrentPosition)
{
	for(bool Enabled : {false, true})
	{
		SCOPED_TRACE(Enabled);
		QmCameraEffects::CFreeviewCameraSmoothing Camera;
		auto Input = FreeviewInput();
		Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
		Input.m_Enabled = Enabled;
		Input.m_Smoothness = Enabled ? 0 : 80;
		EXPECT_EQ(Camera.Update(Input, vec2(50, 0), vec2(200, 0), 0.096f), vec2(200, 0));
		EXPECT_FALSE(Camera.Active());
		Input.m_Enabled = true;
		Input.m_Smoothness = 80;
		EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
	}
}

TEST(QmFreeviewCamera, InvalidFrameTimeKeepsFiniteStateAndNextFrameRecovers)
{
	QmCameraEffects::CFreeviewCameraSmoothing Camera;
	const auto Input = FreeviewInput(IClient::STATE_DEMOPLAYBACK);
	const vec2 Before = Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
	for(float Delta : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		EXPECT_EQ(Camera.Update(Input, Before, vec2(200, 0), Delta), Before);
	const vec2 After = Camera.Update(Input, Before, vec2(200, 0), 0.016f);
	EXPECT_TRUE(std::isfinite(After.x));
	EXPECT_GT(After.x, Before.x);
	EXPECT_LT(After.x, 200.0f);
}

TEST(QmFreeviewCamera, ForcedPositionResetDiscardsPriorAnchor)
{
	QmCameraEffects::CFreeviewCameraSmoothing Camera;
	const auto Input = FreeviewInput();
	Camera.Update(Input, vec2(), vec2(100, 0), 0.096f);
	Camera.Reset();
	EXPECT_FALSE(Camera.Active());
	EXPECT_NEAR(Camera.Update(Input, vec2(200, 0), vec2(400, 0), 0.096f).x, 300.0f, 0.001f);
}

TEST(QmFreeviewCamera, DisabledOptionKeepsNativeTransitionAndEnabledZeroSmoothnessOverridesIt)
{
	for(int State : {IClient::STATE_ONLINE, IClient::STATE_DEMOPLAYBACK})
	{
		SCOPED_TRACE(State);
		auto Input = FreeviewInput(State);
		Input.m_Enabled = false;
		EXPECT_FALSE(QmCameraEffects::UseFreeviewCameraSettings(Input));
		Input.m_Enabled = true;
		Input.m_Smoothness = 0;
		EXPECT_TRUE(QmCameraEffects::UseFreeviewCameraSettings(Input));
		Input.m_Smoothness = 80;
		EXPECT_TRUE(QmCameraEffects::UseFreeviewCameraSettings(Input));
		Input.m_UsePosition = true;
		EXPECT_FALSE(QmCameraEffects::UseFreeviewCameraSettings(Input));
		Input.m_UsePosition = false;
		Input.m_Spectating = false;
		EXPECT_FALSE(QmCameraEffects::UseFreeviewCameraSettings(Input));
	}
}
