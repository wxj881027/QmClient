#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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
