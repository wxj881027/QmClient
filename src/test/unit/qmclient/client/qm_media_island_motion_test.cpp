// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/hud_frozen_tee_state.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/tclient/pet.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace
{
	class CScopedTestConfig
	{
		CConfig m_Saved = g_Config;

	public:
		~CScopedTestConfig() { g_Config = m_Saved; }
	};
}

namespace
{
	void AdvanceIslandRuntime(CUiV2AnimationRuntime &Runtime, float Seconds)
	{
		const float Dt = 1.0f / 60.0f;
		int Steps = static_cast<int>(Seconds / Dt) + 1;
		for(int i = 0; i < Steps; ++i)
			Runtime.Advance(Dt);
	}
	const int StepCount(float Seconds, float FrameSeconds)
	{
		if(Seconds <= 0.0f)
			return 0;
		return static_cast<int>(Seconds / FrameSeconds + 0.999f);
	}
	float StepBlobSpring(SHudMediaIslandBlobSpring &Spring, bool TargetVisible, float Seconds, float FrameSeconds = 1.0f / 60.0f)
	{
		const float Period = QmHudMediaIslandBlobSpringWindowSeconds();
		const int Steps = StepCount(Seconds, FrameSeconds);
		for(int i = 0; i < Steps; ++i)
			QmHudMediaIslandBlobSpringAdvance(Spring, FrameSeconds, Period, TargetVisible);
		return QmHudMediaIslandBlobProgress(Spring);
	}
	float PeakBlobTravel(SHudMediaIslandBlobSpring &Spring, bool TargetVisible, float Seconds, float FrameSeconds = 1.0f / 240.0f)
	{
		const float Period = QmHudMediaIslandBlobSpringWindowSeconds();
		float Peak = 0.0f;
		const int Steps = std::max(1, static_cast<int>(Seconds / FrameSeconds));
		for(int i = 0; i < Steps; ++i)
		{
			QmHudMediaIslandBlobSpringAdvance(Spring, FrameSeconds, Period, TargetVisible);
			Peak = std::max(Peak, QmHudMediaIslandBlobPose(Spring).m_Travel);
		}
		return Peak;
	}
}

TEST(QmHudMediaIslandEntranceSpring, HiddenRelaxesAndReappearInheritsVelocity)
{
	const CScopedTestConfig ConfigScope;
	g_Config.m_QmUiMotionLevel = 2;
	CUiV2AnimationRuntime Runtime;

	QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	AdvanceIslandRuntime(Runtime, 0.06f);
	const SHudMediaIslandEntranceSpringResult BeforeHide = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	EXPECT_GT(BeforeHide.m_DropProgress, 0.0f);
	EXPECT_LT(BeforeHide.m_DropProgress, 1.0f);

	// 隐藏即打断：目标回落 0，弹簧以当前速度回落（不瞬移）。
	QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, false);
	AdvanceIslandRuntime(Runtime, 0.05f);
	const SHudMediaIslandEntranceSpringResult Relaxed = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, false);
	EXPECT_GT(Relaxed.m_DropProgress, 0.0f);
	EXPECT_LT(Relaxed.m_DropProgress, BeforeHide.m_DropProgress);

	// 重现：值连续（不从 0 重放），并继承回落速度平滑掉头继续入场。
	const SHudMediaIslandEntranceSpringResult Reappeared = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	EXPECT_NEAR(Reappeared.m_DropProgress, Relaxed.m_DropProgress, 1e-4f);
	AdvanceIslandRuntime(Runtime, 0.05f);
	const SHudMediaIslandEntranceSpringResult Continued = QmHudMediaIslandResolveEntranceSprings(Runtime, 201, 202, true);
	EXPECT_GT(Continued.m_DropProgress, Reappeared.m_DropProgress);
	EXPECT_LT(Continued.m_DropProgress, 1.0f);
}

TEST(QmHudMediaIslandEntranceSpring, MotionLevelZeroSnapsToSettled)
{
	const CScopedTestConfig ConfigScope;
	g_Config.m_QmUiMotionLevel = 0;
	CUiV2AnimationRuntime Runtime;

	const SHudMediaIslandEntranceSpringResult Result = QmHudMediaIslandResolveEntranceSprings(Runtime, 301, 302, true);
	EXPECT_NEAR(Result.m_DropProgress, 1.0f, 1e-6f);
	EXPECT_NEAR(Result.m_ExpandProgress, 1.0f, 1e-6f);
	EXPECT_FALSE(Runtime.HasActiveAnimation(301, EUiAnimProperty::ALPHA));
	EXPECT_FALSE(Runtime.HasActiveAnimation(302, EUiAnimProperty::ALPHA));
	g_Config.m_QmUiMotionLevel = 2;
}

TEST(QmHudMediaIslandEntranceSpring, CapsuleSqueezeScalesWithAmount)
{
	constexpr float BaseIslandHeight = 32.0f;
	constexpr float PaddingX = 8.0f;
	constexpr float ScreenPadding = 4.0f;
	constexpr float ScreenWidth = 800.0f;

	float X = 0.0f, W = 0.0f, H = 0.0f;
	QmHudMediaIslandApplyCapsuleSqueeze(400.0f, 300.0f, 32.0f, 1.0f, BaseIslandHeight, PaddingX, ScreenPadding, ScreenWidth, X, W, H);
	EXPECT_LT(W, 300.0f);
	EXPECT_GT(H, 30.0f);
	EXPECT_LT(H, 32.0f);
	EXPECT_NEAR(X + W * 0.5f, 400.0f, 1e-4f);

	float NoSqueezeX = 0.0f, NoSqueezeW = 0.0f, NoSqueezeH = 0.0f;
	QmHudMediaIslandApplyCapsuleSqueeze(400.0f, 300.0f, 32.0f, 0.0f, BaseIslandHeight, PaddingX, ScreenPadding, ScreenWidth, NoSqueezeX, NoSqueezeW, NoSqueezeH);
	EXPECT_FLOAT_EQ(NoSqueezeW, 300.0f);
	EXPECT_FLOAT_EQ(NoSqueezeH, 32.0f);
	EXPECT_NEAR(NoSqueezeX + NoSqueezeW * 0.5f, 400.0f, 1e-4f);
}

TEST(QmHudMediaIslandBlob, UnderdampedTravelOvershootsThenPullsBackToRest)
{
	// 关键回归：分离必须是"冲过头再被拉回"，而不是在终点急刹车。
	// 旧实现峰值只有 +1.20%（约 0.15px）且发生在 p≈0.90，肉眼不可见。
	SHudMediaIslandBlobSpring Spring;
	// 峰值与落定都需要覆盖完整窗口（停车判据在窗口走完后才生效）。
	const float SettleSeconds = QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f;
	const float Peak = PeakBlobTravel(Spring, true, SettleSeconds);
	EXPECT_GT(Peak, 1.05f) << "过冲必须明显可见";
	EXPECT_LT(Peak, 1.15f) << "过冲仍需克制";
	// zeta=0.60 的理论过冲 +9.48%；数值积分实测 +9.28%。
	EXPECT_NEAR(Peak, 1.0928f, 0.005f);

	// 峰值之后要回落并稳定在 1.0（精确落位）。
	StepBlobSpring(Spring, true, SettleSeconds);
	const SHudMediaIslandBlobPose Settled = QmHudMediaIslandBlobPose(Spring);
	EXPECT_FLOAT_EQ(Settled.m_Travel, 1.0f);
	// 落定后形变必须回到正圆（速度项归零）。
	EXPECT_FLOAT_EQ(Settled.m_StretchX, 1.0f);
	EXPECT_FLOAT_EQ(Settled.m_StretchY, 1.0f);
	EXPECT_FLOAT_EQ(Settled.m_RadiusScale, 1.0f);
	EXPECT_FLOAT_EQ(Settled.m_ContentAlpha, 1.0f);
}

TEST(QmHudMediaIslandBlob, SettlesExactlyOnTargetOnceTheWindowIsComplete)
{
	// 减少动效（motion level 0）路径必须精确落到 1 / 0，不能留残差。
	SHudMediaIslandBlobSpring Spring;
	QmHudMediaIslandBlobSetBinary(Spring, true);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 1.0f);
	QmHudMediaIslandBlobSetBinary(Spring, false);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);

	// 动画路径走完窗口后同样精确落在目标上（指数尾巴由停车判据收尾）。
	SHudMediaIslandBlobSpring Animated;
	StepBlobSpring(Animated, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Animated).m_Travel, 1.0f);
	EXPECT_FLOAT_EQ(Animated.m_Value, 1.0f);
	EXPECT_FLOAT_EQ(Animated.m_Velocity, 0.0f);
	StepBlobSpring(Animated, false, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Animated).m_Travel, 0.0f);
	EXPECT_FLOAT_EQ(Animated.m_Value, 0.0f);
	EXPECT_FLOAT_EQ(Animated.m_Velocity, 0.0f);
}

TEST(QmHudMediaIslandBlob, OvershootPeaksAfterTheRushAndNotDuringTheBridgePhase)
{
	// 峰值必须出现在行程后段：若峰值落在中段，液滴会在还连着主岛时来回抖。
	SHudMediaIslandBlobSpring Spring;
	constexpr float Frame = 1.0f / 240.0f;
	float Peak = 0.0f;
	float PeakAt = 0.0f;
	float AtQuarter = 0.0f;
	for(int i = 0; i < 320; ++i)
	{
		QmHudMediaIslandBlobSpringAdvance(Spring, Frame, QmHudMediaIslandBlobSpringWindowSeconds(), true);
		const float Travel = QmHudMediaIslandBlobPose(Spring).m_Travel;
		if(i + 1 == 60)
			AtQuarter = Travel;
		if(Travel > Peak)
		{
			Peak = Travel;
			PeakAt = (i + 1) * Frame;
		}
	}
	// 0.25s 时仍在冲向目标，还没有过冲（连接阶段保持稳定）。
	EXPECT_GT(AtQuarter, 0.85f);
	EXPECT_LT(AtQuarter, 1.0f);
	// 理论峰值时刻 π/wd = 0.436s（zeta=0.60, wn=9.0），落在原 0.44s 节奏之内。
	EXPECT_NEAR(PeakAt, 0.436f, 0.03f);
}

TEST(QmHudMediaIslandBlob, TravelIsFrameRateIndependent)
{
	// 解析式求值：串行小步与一次性大步必须给出相同结果。
	SHudMediaIslandBlobSpring Sixty;
	SHudMediaIslandBlobSpring TwoForty;
	float SixtyTravel = 0.0f;
	float TwoFortyTravel = 0.0f;
	for(int i = 0; i < 30; ++i)
		SixtyTravel = StepBlobSpring(Sixty, true, 1.0f / 60.0f, 1.0f / 60.0f);
	for(int i = 0; i < 120; ++i)
		TwoFortyTravel = StepBlobSpring(TwoForty, true, 1.0f / 240.0f, 1.0f / 240.0f);
	EXPECT_FLOAT_EQ(SixtyTravel, TwoFortyTravel);
}

TEST(QmHudMediaIslandBlob, ReverseKeepsVelocityContinuousAndPoseHasNoJump)
{
	SHudMediaIslandBlobSpring Spring;
	StepBlobSpring(Spring, true, 0.220f);
	const SHudMediaIslandBlobPose BeforeReverse = QmHudMediaIslandBlobPose(Spring);
	ASSERT_GT(BeforeReverse.m_Travel, 0.0f);

	// 目标是常数，切目标不换算任何状态，所以位移与速度都逐位保持
	// ——这正是旧实现（进度反向时速度突变）做不到的地方。
	const float ValueBefore = Spring.m_Value;
	const float VelocityBefore = Spring.m_Velocity;
	StepBlobSpring(Spring, false, 0.0f);
	EXPECT_FLOAT_EQ(Spring.m_Value, ValueBefore);
	EXPECT_FLOAT_EQ(Spring.m_Velocity, VelocityBefore);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, BeforeReverse.m_Travel);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Velocity, BeforeReverse.m_Velocity);

	// 收回途中位姿连续回落。
	StepBlobSpring(Spring, false, 0.110f);
	const SHudMediaIslandBlobPose MidReverse = QmHudMediaIslandBlobPose(Spring);
	EXPECT_LT(MidReverse.m_Travel, BeforeReverse.m_Travel);

	// 反向途中再切回，位姿仍然连续（不允许折角/跳变）。
	StepBlobSpring(Spring, true, 0.0f);
	const SHudMediaIslandBlobPose AfterReverse = QmHudMediaIslandBlobPose(Spring);
	EXPECT_FLOAT_EQ(AfterReverse.m_Travel, MidReverse.m_Travel);
	EXPECT_FLOAT_EQ(AfterReverse.m_Velocity, MidReverse.m_Velocity);
	EXPECT_FLOAT_EQ(AfterReverse.m_RadiusScale, MidReverse.m_RadiusScale);
	EXPECT_FLOAT_EQ(AfterReverse.m_StretchX, MidReverse.m_StretchX);
	EXPECT_FLOAT_EQ(AfterReverse.m_StretchY, MidReverse.m_StretchY);
}

TEST(QmHudMediaIslandBlob, VelocityStretchIsSubtleAndReturnsToACircleAtRest)
{
	SHudMediaIslandBlobSpring Spring;
	StepBlobSpring(Spring, true, 0.120f);
	const SHudMediaIslandBlobPose Moving = QmHudMediaIslandBlobPose(Spring);
	EXPECT_GT(Moving.m_StretchX, 1.0f);
	EXPECT_LT(Moving.m_StretchY, 1.0f);
	EXPECT_GT(Moving.m_Velocity, 0.0f) << "拉伸必须由真实弹簧速度驱动";

	// 旧实现里速度通道是死代码（Pose 从未取用速度函数），这里确认它真的接上了。
	const SHudMediaIslandBlobPose PeakStretch = QmHudMediaIslandBlobPose(Spring);
	EXPECT_GE(PeakStretch.m_StretchX, Moving.m_StretchX);

	SHudMediaIslandBlobSpring Settled;
	StepBlobSpring(Settled, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	const SHudMediaIslandBlobPose AtRest = QmHudMediaIslandBlobPose(Settled);
	EXPECT_FLOAT_EQ(AtRest.m_StretchX, 1.0f);
	EXPECT_FLOAT_EQ(AtRest.m_StretchY, 1.0f);
	EXPECT_FLOAT_EQ(AtRest.m_Velocity, 0.0f);
}

TEST(QmHudMediaIslandBlob, StretchStaysWithinTheHistoricBounds)
{
	SHudMediaIslandBlobSpring Spring;
	constexpr float Frame = 1.0f / 240.0f;
	float MaxStretchX = 1.0f;
	float MinStretchY = 1.0f;
	for(int i = 0; i < 320; ++i)
	{
		QmHudMediaIslandBlobSpringAdvance(Spring, Frame, QmHudMediaIslandBlobSpringWindowSeconds(), true);
		const SHudMediaIslandBlobPose Pose = QmHudMediaIslandBlobPose(Spring);
		MaxStretchX = std::max(MaxStretchX, Pose.m_StretchX);
		MinStretchY = std::min(MinStretchY, Pose.m_StretchY);
	}
	EXPECT_LE(MaxStretchX, 1.09f);
	EXPECT_GE(MinStretchY, 0.95f);
}

TEST(QmHudMediaIslandBlob, SmoothMergeDetachesAtRestAndRemainsDuringTravel)
{
	const float Blend = QmHudMediaIslandBlobBlend(8.0f, 1.0f);
	EXPECT_GT(Blend, 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobBlend(8.0f, 0.0f), 0.0f);

	SHudMediaIslandBlobSpring Moving;
	StepBlobSpring(Moving, true, 0.100f);
	SHudMediaIslandBlobSpring Settled;
	StepBlobSpring(Settled, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	const float MovingConnection = QmHudMediaIslandBlobConnectionStrength(QmHudMediaIslandBlobPose(Moving).m_Travel);
	const float SettledConnection = QmHudMediaIslandBlobConnectionStrength(QmHudMediaIslandBlobPose(Settled).m_Travel);
	EXPECT_GT(MovingConnection, 0.0f);
	EXPECT_FLOAT_EQ(SettledConnection, 0.0f);
	const float NearBridge = QmHudMediaIslandSdfSmoothUnion(1.0f, 1.0f, Blend * MovingConnection);
	const float SettledGap = QmHudMediaIslandSdfSmoothUnion(1.5f, 1.5f, Blend * SettledConnection);
	EXPECT_LT(NearBridge, 0.0f);
	EXPECT_FLOAT_EQ(SettledGap, 1.5f);
}

TEST(QmHudMediaIslandSpectatorEye, OpeningTransitionHonorsMotionLevel)
{
	EXPECT_LT(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.179f, 2), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.180f, 2), 1.0f);
	EXPECT_LT(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.080f, 1), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.0f, 0.081f, 1), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.3f, 0.001f, 0), 1.0f);
	EXPECT_FLOAT_EQ(QmHudAdvanceMediaIslandSpectatorIconProgress(0.3f, -1.0f, 2), 0.3f);
}

TEST(QmHudMediaIslandSpectatorEye, ApprovedOpeningPoseCrossfadesAndOpensVertically)
{
	const SHudMediaIslandSpectatorIconPose Closed = QmHudMediaIslandSpectatorIconPose(0.0f);
	EXPECT_FLOAT_EQ(Closed.m_ClosedAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Closed.m_OpenAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Closed.m_OpenScaleX, 0.88f);
	EXPECT_FLOAT_EQ(Closed.m_OpenScaleY, 0.44f);
	EXPECT_FLOAT_EQ(Closed.m_CountAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Closed.m_CountOffsetX, -2.1f);

	const SHudMediaIslandSpectatorIconPose Mid = QmHudMediaIslandSpectatorIconPose(0.5f);
	EXPECT_FLOAT_EQ(Mid.m_ClosedAlpha, 0.5f);
	EXPECT_FLOAT_EQ(Mid.m_OpenAlpha, 0.5f);

	const SHudMediaIslandSpectatorIconPose Open = QmHudMediaIslandSpectatorIconPose(1.0f);
	EXPECT_FLOAT_EQ(Open.m_ClosedAlpha, 0.0f);
	EXPECT_FLOAT_EQ(Open.m_OpenAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_OpenScaleX, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_OpenScaleY, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_CountAlpha, 1.0f);
	EXPECT_FLOAT_EQ(Open.m_CountOffsetX, 0.0f);
}

TEST(QmHudMediaIslandSpectatorEye, ClosingProgressFollowsTheRightCapsuleRetraction)
{
	// 收回与分离共用同一对 (zeta, wn)。收回途中进度应落在 0..1 之间，
	// 图标按同一比例闭合。
	SHudMediaIslandBlobSpring Spring;
	StepBlobSpring(Spring, true, 0.20f);
	ASSERT_GT(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);
	const float LiquidProgress = StepBlobSpring(Spring, false, 0.06f);
	const float IconProgress = QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 1.0f, LiquidProgress);
	EXPECT_GT(LiquidProgress, 0.0f);
	EXPECT_LT(LiquidProgress, 1.0f);
	EXPECT_NEAR(IconProgress, LiquidProgress, 0.0001f);

	// 收回走完必须精确归零（飞过生成点的那点冲量会被采样端夹住）。
	StepBlobSpring(Spring, false, QmHudMediaIslandBlobSpringWindowSeconds() * 3.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobPose(Spring).m_Travel, 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandBlobProgress(Spring), 0.0f);

	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.25f, 0.25f), 1.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.25f, 0.125f), 0.5f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.25f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 0.0f, 0.0f), 0.0f);

	const SHudMediaIslandSpectatorIconPose HalfClosed = QmHudMediaIslandSpectatorIconPose(0.5f);
	EXPECT_FLOAT_EQ(HalfClosed.m_OpenAlpha, 0.5f);
	EXPECT_FLOAT_EQ(HalfClosed.m_ClosedAlpha, 0.5f);
}

TEST(QmHudMediaIslandSpectatorEye, ReopeningContinuesFromTheCurrentClosingPose)
{
	const float HalfClosed = QmHudMediaIslandSpectatorIconProgressDuringExit(1.0f, 1.0f, 0.5f);
	const float Reopened = QmHudAdvanceMediaIslandSpectatorIconProgress(HalfClosed, 0.045f, 2);
	EXPECT_NEAR(Reopened, 0.75f, 0.0001f);
}

TEST(QmHudMediaIslandSpectatorEye, ReopensOnlyWhileTheRightCapsuleIsBeingReclaimed)
{
	EXPECT_TRUE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(true, false, 0.4f));
	EXPECT_FALSE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(true, false, 0.0f));
	EXPECT_FALSE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(true, true, 0.4f));
	EXPECT_FALSE(QmHudMediaIslandShouldAnimateSpectatorEyeOpen(false, false, 0.4f));
	EXPECT_FLOAT_EQ(QmHudMediaIslandSpectatorCountAlpha(false, QmHudMediaIslandSpectatorIconPose(1.0f)), 0.0f);
}

TEST(QmHudMediaIslandBlob, RightCapsuleSettlesOutsideMainIsland)
{
	SHudMediaIslandBlobSpring SettledSpring;
	StepBlobSpring(SettledSpring, true, QmHudMediaIslandBlobSpringWindowSeconds() * 2.5f);
	const SHudMediaIslandLiquidCapsule Capsule = QmHudMediaIslandRightBlobCapsule(100.0f, 20.0f, 8.0f, 24.0f, 4.0f, QmHudMediaIslandBlobPose(SettledSpring));

	EXPECT_FLOAT_EQ(Capsule.m_Rect.x, 104.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Rect.y, 12.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Rect.w, 24.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Rect.h, 16.0f);
	EXPECT_FLOAT_EQ(Capsule.m_Radius, 8.0f);
	EXPECT_FLOAT_EQ(Capsule.m_SmoothUnion, 0.0f);
	EXPECT_FLOAT_EQ(Capsule.m_ContentAlpha, 1.0f);
}

TEST(QmHudMediaIslandBlob, RightCapsuleUsesTheSameBoundedVelocityStretch)
{
	SHudMediaIslandBlobSpring MovingSpring;
	StepBlobSpring(MovingSpring, true, 0.120f);
	const SHudMediaIslandBlobPose MovingPose = QmHudMediaIslandBlobPose(MovingSpring);
	const SHudMediaIslandLiquidCapsule Capsule = QmHudMediaIslandRightBlobCapsule(100.0f, 20.0f, 8.0f, 24.0f, 4.0f, MovingPose);
	EXPECT_GT(Capsule.m_Rect.w, Capsule.m_Rect.h);
	EXPECT_GT(Capsule.m_SmoothUnion, 0.0f);
	EXPECT_GT(Capsule.m_ContentAlpha, 0.0f);
}
