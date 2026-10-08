// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/qmclient/hook_countdown.h>

#include <gtest/gtest.h>
#include <test/test.h>

namespace
{
	SQmHookCountdownInput PlayerHook()
	{
		SQmHookCountdownInput Input;
		Input.m_ClientId = 1;
		Input.m_Connection = 0;
		Input.m_HookedPlayer = 2;
		Input.m_HookAttached = true;
		Input.m_SourcePosition = vec2(100.0f, 200.0f);
		Input.m_TargetPosition = vec2(300.0f, 100.0f);
		return Input;
	}
}

TEST(QmHudHookCountdown, RemainingProgressUsesTheActualHookCounter)
{
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(0.0f, 1.25f, false), 1.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(30.0f, 1.25f, false), 0.5f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(60.0f, 1.25f, false), 0.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(80.0f, 1.25f, false), 0.0f);
}

TEST(QmHudHookCountdown, TuningUsesTheLaunchCounterInsteadOfAddingRetractTime)
{
	// hook_duration = 0.25 时从第 50 tick 起算，到第 60 tick 脱钩。
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(50.0f, 0.25f, false), 1.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(55.0f, 0.25f, false), 0.5f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(60.0f, 0.25f, false), 0.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(5.0f, 2.25f, false), 0.5f);
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(60.0f, 0.0f, false), 0.0f);
}

TEST(QmHudHookCountdown, InterpolationOnlyBlendsSamplesFromTheSameGrab)
{
	EXPECT_FLOAT_EQ(QmHookCountdownInterpolatedTick(20, 22, 0.5f, true), 21.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownInterpolatedTick(20, 22, -1.0f, true), 20.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownInterpolatedTick(20, 22, 2.0f, true), 22.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownInterpolatedTick(20, 22, 0.5f, false), 22.0f);
	EXPECT_FLOAT_EQ(QmHookCountdownInterpolatedTick(50, 1, 0.5f, true), 1.0f);
}

TEST(QmHudHookCountdown, FractionalLaunchCounterUsesTheCoreIntegerRounding)
{
	EXPECT_FLOAT_EQ(QmHookCountdownProgress(30.0f, 1.24f, false), 0.5f);
}

TEST(QmHudHookCountdown, RingStaysAtTheCurrentHookMidpointWithoutLag)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.1f);
	EXPECT_EQ(Ring.Visual().m_Position, vec2(200.0f, 150.0f));
	Input.m_SourcePosition = vec2(200.0f, 300.0f);
	Input.m_TargetPosition = vec2(800.0f, 500.0f);
	Ring.Update(Input, 0.001f);
	EXPECT_EQ(Ring.Visual().m_Position, vec2(500.0f, 400.0f));
}

TEST(QmHudHookCountdown, EntryAnimatesTheRingWithoutReplacingRemainingProgress)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_HookTick = 30.0f;
	Ring.Update(Input, 0.0f);
	const float InitialAlpha = Ring.Visual().m_Alpha;
	EXPECT_GT(InitialAlpha, 0.0f);
	EXPECT_LT(Ring.Visual().m_Scale, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.5f);
	Ring.Update(Input, 0.07f);
	EXPECT_GT(Ring.Visual().m_Alpha, InitialAlpha);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.5f);
	Ring.Update(Input, 0.1f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Scale, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.5f);
}

TEST(QmHudHookCountdown, ReleaseFreezesProgressAndShrinksWithinOneHundredEightyMilliseconds)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_HookTick = 30.0f;
	Ring.Update(Input, 0.2f);
	const vec2 Position = Ring.Visual().m_Position;
	Input.m_HookAttached = false;
	Input.m_HookTick = 0.0f;
	Input.m_TargetPosition = vec2();
	Ring.Update(Input, 0.09f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.5f);
	EXPECT_EQ(Ring.Visual().m_Position, Position);
	EXPECT_GT(Ring.Visual().m_Alpha, 0.0f);
	EXPECT_LT(Ring.Visual().m_Alpha, 1.0f);
	EXPECT_LT(Ring.Visual().m_Scale, 1.0f);
	Ring.Update(Input, 0.09f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, CounterCorrectionsDoNotRestartTheEntryAnimation)
{
	CQmHookCountdown Corrected;
	CQmHookCountdown Reference;
	auto Input = PlayerHook();
	Input.m_HookTick = 30.0f;
	Corrected.Update(Input, 0.04f);
	Reference.Update(Input, 0.04f);
	Reference.Update(Input, 0.02f);
	Input.m_HookTick = 28.0f;
	Corrected.Update(Input, 0.02f);
	EXPECT_FLOAT_EQ(Corrected.Visual().m_Alpha, Reference.Visual().m_Alpha);
	EXPECT_FLOAT_EQ(Corrected.Visual().m_Scale, Reference.Visual().m_Scale);
	EXPECT_GT(Corrected.Visual().m_Progress, Reference.Visual().m_Progress);
}

TEST(QmHudHookCountdown, RehookContinuesTheVisibleTransitionAndUsesTheNewCounter)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_HookTick = 45.0f;
	Ring.Update(Input, 0.2f);
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.09f);
	const auto BeforeRehook = Ring.Visual();
	Input.m_HookAttached = true;
	Input.m_HookTick = 6.0f;
	Ring.Update(Input, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, BeforeRehook.m_Alpha);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Scale, BeforeRehook.m_Scale);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.9f);
	Ring.Update(Input, 0.07f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Scale, 1.0f);
}

TEST(QmHudHookCountdown, ChangingTargetImmediatelyUsesTheNewHookMidpoint)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.2f);
	Input.m_HookedPlayer = 3;
	Input.m_HookTick = 6.0f;
	Input.m_TargetPosition = vec2(500.0f, 600.0f);
	Ring.Update(Input, 0.0f);
	EXPECT_EQ(Ring.Visual().m_Position, vec2(300.0f, 400.0f));
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.9f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 1.0f);
}

TEST(QmHudHookCountdown, ShortGrabCanExitBeforeTheEntryFinishes)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.01f);
	const auto BeforeRelease = Ring.Visual();
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.09f);
	EXPECT_LT(Ring.Visual().m_Alpha, BeforeRelease.m_Alpha);
	EXPECT_LE(Ring.Visual().m_Scale, BeforeRelease.m_Scale);
	Ring.Update(Input, 0.1f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, WallHookNeverStartsARing)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_HookedPlayer = -1;
	Ring.Update(Input, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, EndlessHookStaysFullAndBlue)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Input.m_HookTick = 1000.0f;
	Ring.Update(Input, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 1.0f);
	EXPECT_GT(Ring.Visual().m_Color.b, Ring.Visual().m_Color.r);
	EXPECT_GT(Ring.Visual().m_Color.b, Ring.Visual().m_Color.g);
}

TEST(QmHudHookCountdown, WarningColorTurnsWarmAsTheRealProgressRunsOut)
{
	const ColorRGBA Normal = QmHookCountdownColor(0.75f);
	const ColorRGBA Warning = QmHookCountdownColor(0.20f);
	const ColorRGBA Urgent = QmHookCountdownColor(0.0f);
	EXPECT_GT(Normal.b, Normal.r);
	EXPECT_GT(Warning.r, Warning.b);
	EXPECT_GT(Urgent.r, Urgent.g);
	EXPECT_LT(Urgent.g, Warning.g);
}

TEST(QmHudHookCountdown, SwitchingConnectionDropsThePreviousRing)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.2f);
	Input.m_Connection = 1;
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, InvalidOwnerImmediatelyClearsTheRing)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.2f);
	Input.m_ClientId = -1;
	Ring.Update(Input, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, SwitchingClientStartsANewRingWithoutTheOldTransition)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.2f);
	Input.m_ClientId = 3;
	Input.m_HookTick = 45.0f;
	Ring.Update(Input, 0.0f);
	EXPECT_LT(Ring.Visual().m_Alpha, 1.0f);
	EXPECT_LT(Ring.Visual().m_Scale, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.25f);
}

TEST(QmHudHookCountdown, ResetClearsAnActiveOrExitingRing)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.2f);
	Ring.Reset();
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
	Ring.Update(Input, 0.2f);
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.01f);
	Ring.Reset();
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, UnchangedCounterDoesNotCountDownWithWallClockTime)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_HookTick = 30.0f;
	Ring.Update(Input, 0.2f);
	Ring.Update(Input, 5.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 0.5f);
}

TEST(QmHudHookCountdown, ALongFrameCompletesReleaseWithoutLeavingATail)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Ring.Update(Input, 0.2f);
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.5f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, AnimationMatchesAcrossDifferentFrameSteps)
{
	CQmHookCountdown FastFrames;
	CQmHookCountdown SlowFrames;
	auto Input = PlayerHook();
	FastFrames.Update(Input, 0.0f);
	SlowFrames.Update(Input, 0.0f);
	for(int Frame = 0; Frame < 4; ++Frame)
		FastFrames.Update(Input, 0.02f);
	SlowFrames.Update(Input, 0.08f);
	EXPECT_NEAR(FastFrames.Visual().m_Alpha, SlowFrames.Visual().m_Alpha, 0.00001f);
	EXPECT_NEAR(FastFrames.Visual().m_Scale, SlowFrames.Visual().m_Scale, 0.00001f);
	Input.m_HookAttached = false;
	FastFrames.Update(Input, 0.0f);
	SlowFrames.Update(Input, 0.0f);
	for(int Frame = 0; Frame < 4; ++Frame)
		FastFrames.Update(Input, 0.03f);
	SlowFrames.Update(Input, 0.12f);
	EXPECT_NEAR(FastFrames.Visual().m_Alpha, SlowFrames.Visual().m_Alpha, 0.00001f);
	EXPECT_NEAR(FastFrames.Visual().m_Scale, SlowFrames.Visual().m_Scale, 0.00001f);
}

// SDF 羽化宽度用的像素比例：开关环与钩子环共用，取 x/y 里较大的方向。
TEST(QmHudHookCountdown, ScreenPixelSizeUsesTheWiderAxisAndGuardsZeroSize)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 200.0f, 100.0f, 100, 100), 2.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 100.0f, 300.0f, 100, 100), 3.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 100.0f, 100.0f, 0, 0), 100.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 400.0f, 300.0f, 800, 600), 0.5f);
}
