// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <game/client/QmUi/QmHookCountdownRender.h>
#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/qmclient/hook_countdown.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cmath>
#include <limits>
#include <tuple>

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

TEST(QmHudHookCountdown, EndlessHookDoesNotConsumeTimedProgress)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Input.m_HookTick = 1000.0f;
	Ring.Update(Input, 1.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Progress, 1.0f);
	EXPECT_TRUE(Ring.Visual().m_EndlessHook);
}

TEST(QmHudHookCountdown, EndlessHookAdvancesTheFlowPhaseAndLoops)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.0f);
	EXPECT_TRUE(Ring.Visual().m_EndlessHook);
	Ring.Update(Input, QM_HOOK_COUNTDOWN_FLOW_PERIOD_SECONDS * 0.25f);
	EXPECT_NEAR(Ring.Visual().m_FlowPhase, 0.25f, 0.0001f);
	// 越过一圈后按 fmod 循环回小相位，彩虹持续流动不溢出。
	Ring.Update(Input, QM_HOOK_COUNTDOWN_FLOW_PERIOD_SECONDS * 0.9f);
	EXPECT_NEAR(Ring.Visual().m_FlowPhase, 0.15f, 0.0001f);
}

TEST(QmHudHookCountdown, NormalHookDoesNotUseTheFlowOverlay)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_HookTick = 30.0f;
	Ring.Update(Input, 0.2f);
	EXPECT_FALSE(Ring.Visual().m_EndlessHook);
	EXPECT_FLOAT_EQ(Ring.Visual().m_FlowPhase, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_BreathPhase, 0.0f);
}

TEST(QmHudHookCountdown, EndlessBreathingLoopsIndependentlyOfRainbowFlow)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_BreathPhase, 0.0f);
	Ring.Update(Input, QM_HOOK_COUNTDOWN_BREATH_PERIOD_SECONDS * 0.5f);
	EXPECT_NEAR(Ring.Visual().m_BreathPhase, 0.5f, 0.0001f);
	EXPECT_GT(QmHookCountdownBreath(Ring.Visual().m_BreathPhase), 0.99f);
	EXPECT_NE(Ring.Visual().m_FlowPhase, Ring.Visual().m_BreathPhase);
	Ring.Update(Input, QM_HOOK_COUNTDOWN_BREATH_PERIOD_SECONDS * 0.5f);
	EXPECT_NEAR(Ring.Visual().m_BreathPhase, 0.0f, 0.0001f);
	EXPECT_LT(QmHookCountdownBreath(Ring.Visual().m_BreathPhase), 0.01f);
}

// 彩虹调色板与好友跟随光环共用：色相按 1.0 循环、主色方位正确，且亮度有下限，
// 细描边在暗背景上才不会掉成一条暗色轮廓。
TEST(QmNeonRainbow, CyclesHueAndLiftsBrightnessForThinStrokes)
{
	const auto At = [](float Hue, float Wave = 0.0f) {
		const ColorRGBA Color = QmNeonRainbowColor(Hue, Wave, 1.0f);
		return std::tuple{Color.r, Color.g, Color.b, Color.a};
	};
	// 三个主色方位各自主导，1.0 与 0.0 是同一圈。
	const auto [RedR, RedG, RedB, RedA] = At(0.0f);
	EXPECT_GT(RedR, RedG);
	EXPECT_GT(RedR, RedB);
	EXPECT_FLOAT_EQ(RedA, 1.0f);
	const auto [GreenR, GreenG, GreenB, GreenA] = At(1.0f / 3.0f);
	EXPECT_GT(GreenG, GreenR);
	EXPECT_GT(GreenG, GreenB);
	const auto [BlueR, BlueG, BlueB, BlueA] = At(2.0f / 3.0f);
	EXPECT_GT(BlueB, BlueR);
	EXPECT_GT(BlueB, BlueG);
	EXPECT_EQ(At(1.25f), At(0.25f));
	// 混入白色后没有接近纯黑的分量，最暗的通道也保留一段亮度。
	EXPECT_GE(RedB, 0.15f);
	// 高光（Wave=1）只提亮不换色相：三个通道一起变亮。
	const auto [DimR, DimG, DimB, DimA] = At(0.4f, 0.0f);
	const auto [BrightR, BrightG, BrightB, BrightA] = At(0.4f, 1.0f);
	EXPECT_GT(BrightR, DimR);
	EXPECT_GT(BrightG, DimG);
	EXPECT_GT(BrightB, DimB);
}

TEST(QmHudHookCountdown, ReleaseKeepsFlowingUntilTheRingFadesOut)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.2f);
	const float PhaseBeforeRelease = Ring.Visual().m_FlowPhase;
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.05f);
	EXPECT_GT(Ring.Visual().m_FlowPhase, PhaseBeforeRelease);
	EXPECT_GT(Ring.Visual().m_Alpha, 0.0f);
}

// 环色按剩余时间走三段（普通 / 警告 / 紧急），三段都由配置给出：
// 取色卡里的三个色块必须真的决定环在对应阶段的颜色。
TEST(QmHudHookCountdown, RingWalksTheConfiguredStageColorsAsTimeRunsOut)
{
	const int SavedNormal = g_Config.m_QmHookCountdownColor;
	const int SavedWarning = g_Config.m_QmHookCountdownWarningColor;
	const int SavedUrgent = g_Config.m_QmHookCountdownUrgentColor;
	const ColorRGBA Normal(0.20f, 0.65f, 1.0f, 1.0f);
	const ColorRGBA Warning(1.0f, 0.70f, 0.15f, 1.0f);
	const ColorRGBA Urgent(1.0f, 0.25f, 0.25f, 1.0f);
	g_Config.m_QmHookCountdownColor = color_cast<ColorHSLA>(Normal).Pack(true);
	g_Config.m_QmHookCountdownWarningColor = color_cast<ColorHSLA>(Warning).Pack(true);
	g_Config.m_QmHookCountdownUrgentColor = color_cast<ColorHSLA>(Urgent).Pack(true);
	const auto ExpectColor = [](const ColorRGBA &Actual, const ColorRGBA &Expected) {
		EXPECT_NEAR(Actual.r, Expected.r, 0.01f);
		EXPECT_NEAR(Actual.g, Expected.g, 0.01f);
		EXPECT_NEAR(Actual.b, Expected.b, 0.01f);
	};
	ExpectColor(QmHookCountdownColor(1.0f), Normal);
	ExpectColor(QmHookCountdownColor(0.45f), Normal);
	ExpectColor(QmHookCountdownColor(0.20f), Warning);
	ExpectColor(QmHookCountdownColor(0.0f), Urgent);
	// 段与段之间连续过渡：中间值必须落在两端之间，不能直接跳色。
	const ColorRGBA Blend = QmHookCountdownColor(0.325f);
	EXPECT_GT(Blend.r, Normal.r);
	EXPECT_LT(Blend.r, Warning.r);
	// 换掉普通段配置，环的起步颜色就跟着换。
	g_Config.m_QmHookCountdownColor = color_cast<ColorHSLA>(ColorRGBA(0.30f, 0.90f, 0.40f, 1.0f)).Pack(true);
	EXPECT_GT(QmHookCountdownColor(0.90f).g, 0.85f);
	g_Config.m_QmHookCountdownColor = SavedNormal;
	g_Config.m_QmHookCountdownWarningColor = SavedWarning;
	g_Config.m_QmHookCountdownUrgentColor = SavedUrgent;
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

// 尺寸百分比按 100% 为基准换算；非法/零值回退到默认，避免半径塌成 0。
TEST(QmHudHookCountdown, CountdownSizeScaleMapsPercentToMultiplier)
{
	g_Config.m_QmHookCountdownSize = 100;
	EXPECT_FLOAT_EQ(qm_hook_countdown_ui::CountdownSizeScale(), 1.0f);
	g_Config.m_QmHookCountdownSize = 50;
	EXPECT_FLOAT_EQ(qm_hook_countdown_ui::CountdownSizeScale(), 0.5f);
	g_Config.m_QmHookCountdownSize = 200;
	EXPECT_FLOAT_EQ(qm_hook_countdown_ui::CountdownSizeScale(), 2.0f);
	// 0 在配置范围外，仍要给出可用的默认值而不是零半径。
	g_Config.m_QmHookCountdownSize = 0;
	EXPECT_FLOAT_EQ(qm_hook_countdown_ui::CountdownSizeScale(), 1.0f);
	g_Config.m_QmHookCountdownSize = 100;
}

// 无限钩样式只有两档：0 为 ∞ 符号，1 为圆环；越界值不应落到圆环。
TEST(QmHudHookCountdown, EndlessStyleSelectsRingOnlyForExplicitRingValue)
{
	g_Config.m_QmHookCountdownEndlessStyle = 0;
	EXPECT_FALSE(qm_hook_countdown_ui::EndlessHookUsesRing());
	g_Config.m_QmHookCountdownEndlessStyle = 1;
	EXPECT_TRUE(qm_hook_countdown_ui::EndlessHookUsesRing());
	g_Config.m_QmHookCountdownEndlessStyle = 2;
	EXPECT_FALSE(qm_hook_countdown_ui::EndlessHookUsesRing());
	g_Config.m_QmHookCountdownEndlessStyle = 0;
}

// SDF 羽化宽度用的像素比例：开关环与钩子环共用，取 x/y 里较大的方向。
TEST(QmHudHookCountdown, ScreenPixelSizeUsesTheWiderAxisAndGuardsZeroSize)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 200.0f, 100.0f, 100, 100), 2.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 100.0f, 300.0f, 100, 100), 3.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 100.0f, 100.0f, 0, 0), 100.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScreenPixelSize(0.0f, 0.0f, 400.0f, 300.0f, 800, 600), 0.5f);
}

TEST(QmHudHookCountdown, InvalidFrameTimeKeepsEndlessAnimationFiniteAndRecoverable)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.2f);
	const auto Before = Ring.Visual();
	for(float Delta : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		Ring.Update(Input, Delta);
		EXPECT_FLOAT_EQ(Ring.Visual().m_FlowPhase, Before.m_FlowPhase);
		EXPECT_FLOAT_EQ(Ring.Visual().m_BreathPhase, Before.m_BreathPhase);
		EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, Before.m_Alpha);
		EXPECT_FLOAT_EQ(Ring.Visual().m_Scale, Before.m_Scale);
	}
	Ring.Update(Input, 0.1f);
	EXPECT_GT(Ring.Visual().m_FlowPhase, Before.m_FlowPhase);
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.2f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
}

TEST(QmHudHookCountdown, EndlessHookCanReturnToTimedHookAndResetForAnotherConnection)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.2f);
	Input.m_EndlessHook = false;
	Input.m_HookTick = 50.0f;
	Ring.Update(Input, 0.1f);
	EXPECT_FALSE(Ring.Visual().m_EndlessHook);
	EXPECT_LT(Ring.Visual().m_Progress, 1.0f);
	Input.m_Connection = 1;
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_FlowPhase, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_BreathPhase, 0.0f);
	EXPECT_TRUE(Ring.Visual().m_EndlessHook);
}

TEST(QmHudHookCountdown, RainbowTravelsAlongThePathAndClosesWithoutAColorSeam)
{
	const auto At = [](float Phase, float PathPosition) {
		const ColorRGBA Color = QmHookCountdownFlowColor(Phase, PathPosition);
		return std::tuple{Color.r, Color.g, Color.b, Color.a};
	};
	EXPECT_NE(At(0.0f, 0.0f), At(0.25f, 0.0f));
	EXPECT_NE(At(0.0f, 0.0f), At(0.0f, 0.25f));
	// 色相在时间前进后到达更远的位置。亮度另有自己的高光节奏，所以这里只比较色相。
	const auto HueAt = [](float Phase, float PathPosition) {
		return color_cast<ColorHSLA>(QmHookCountdownFlowColor(Phase, PathPosition)).h;
	};
	EXPECT_NEAR(HueAt(0.0f, 0.0f), HueAt(0.25f, 0.25f), 0.002f);
	// 走完整圈后首尾同色，环与 ∞ 的接缝处不会跳色；相位绕满一圈也回到同一帧。
	EXPECT_EQ(At(0.25f, 0.0f), At(0.25f, 1.0f));
	EXPECT_EQ(At(0.0f, 0.0f), At(1.0f, 0.0f));
}

TEST(QmHudHookInfinity, ClosedPathKeepsFiniteUnitNormalsForTheGlow)
{
	const auto &aPath = QmHookCountdownInfinityPath();
	EXPECT_EQ(aPath.front().m_Position, aPath.back().m_Position);
	EXPECT_EQ(aPath.front().m_Normal, aPath.back().m_Normal);
	EXPECT_FLOAT_EQ(aPath.front().m_PathPosition, 0.0f);
	EXPECT_FLOAT_EQ(aPath.back().m_PathPosition, 1.0f);
	for(size_t i = 0; i < aPath.size(); ++i)
	{
		SCOPED_TRACE(i);
		EXPECT_TRUE(std::isfinite(aPath[i].m_Position.x));
		EXPECT_TRUE(std::isfinite(aPath[i].m_Position.y));
		EXPECT_NEAR(length(aPath[i].m_Normal), 1.0f, 0.0001f);
		if(i != 0)
			EXPECT_GT(aPath[i].m_PathPosition, aPath[i - 1].m_PathPosition);
	}
}

TEST(QmHudHookInfinity, PathCrossesTheCenterBetweenSymmetricLobes)
{
	const auto &aPath = QmHookCountdownInfinityPath();
	const size_t Quarter = (aPath.size() - 1) / 4;
	EXPECT_GT(aPath.front().m_Position.x, 0.0f);
	EXPECT_LT(aPath[Quarter * 2].m_Position.x, 0.0f);
	EXPECT_NEAR(length(aPath[Quarter].m_Position), 0.0f, 0.0001f);
	EXPECT_NEAR(length(aPath[Quarter * 3].m_Position), 0.0f, 0.0001f);
	EXPECT_NEAR(aPath.front().m_Position.x, -aPath[Quarter * 2].m_Position.x, 0.0001f);
}

// 圆环的逐顶点采样是"彩虹真的绕环"的来源：闭合、单位法线、半圈色差明显，
// 相位前进后同一点换色（流动）。任何一项退化成单色平涂都会让光环失去渐变。
TEST(QmHookCountdownRingSamples, ClosesTheLoopAndCarriesTheRainbowAroundTheRing)
{
	const auto aSamples = qm_hook_countdown_ui::QmHookCountdownRingSamples(0.0f);
	ASSERT_EQ(aSamples.size(), static_cast<size_t>(qm_hook_countdown_ui::RING_SEGMENTS) + 1);
	const auto &First = aSamples.front();
	const auto &Last = aSamples.back();
	EXPECT_NEAR(First.m_Color.r, Last.m_Color.r, 0.0001f);
	EXPECT_NEAR(First.m_Color.g, Last.m_Color.g, 0.0001f);
	EXPECT_NEAR(First.m_Color.b, Last.m_Color.b, 0.0001f);
	EXPECT_NEAR(First.m_Outward.x, Last.m_Outward.x, 0.0001f);
	EXPECT_NEAR(First.m_Outward.y, Last.m_Outward.y, 0.0001f);
	for(const auto &Sample : aSamples)
		EXPECT_NEAR(length(Sample.m_Outward), 1.0f, 0.0001f);
	const auto &Half = aSamples[qm_hook_countdown_ui::RING_SEGMENTS / 2];
	const float HalfDistance = std::abs(First.m_Color.r - Half.m_Color.r) +
				   std::abs(First.m_Color.g - Half.m_Color.g) +
				   std::abs(First.m_Color.b - Half.m_Color.b);
	EXPECT_GT(HalfDistance, 0.4f);
	// 相位推进后环上同一点的颜色必须变化，否则看到的是一圈静态彩虹。
	const auto aFlowed = qm_hook_countdown_ui::QmHookCountdownRingSamples(0.3f);
	EXPECT_GT(std::abs(aSamples[0].m_Color.r - aFlowed[0].m_Color.r) +
			  std::abs(aSamples[0].m_Color.g - aFlowed[0].m_Color.g) +
			  std::abs(aSamples[0].m_Color.b - aFlowed[0].m_Color.b),
		0.05f);
}

// 光晕必须是"外宽内淡、内窄外浓"的连续衰减：三层顺序写反会变成三道同宽的硬环，
// 呼吸只改强度不改层次，吸气时更强也更散。
TEST(QmHookCountdownGlowLayers, StacksWideAndFaintOutsideNarrowAndBrightInside)
{
	for(const float Breath : {0.0f, 0.5f, 1.0f})
	{
		SCOPED_TRACE(Breath);
		const auto aLayers = qm_hook_countdown_ui::QmHookCountdownGlowLayers(Breath);
		for(size_t i = 0; i < aLayers.size(); ++i)
		{
			EXPECT_GT(aLayers[i].m_Alpha, 0.0f);
			EXPECT_LE(aLayers[i].m_Spread, qm_hook_countdown_ui::GLOW_EXTENT);
			if(i > 0)
			{
				EXPECT_LT(aLayers[i].m_Spread, aLayers[i - 1].m_Spread);
				EXPECT_GT(aLayers[i].m_Alpha, aLayers[i - 1].m_Alpha);
			}
		}
	}
	const auto aRest = qm_hook_countdown_ui::QmHookCountdownGlowLayers(0.0f);
	const auto aPeak = qm_hook_countdown_ui::QmHookCountdownGlowLayers(1.0f);
	for(size_t i = 0; i < aRest.size(); ++i)
	{
		EXPECT_GT(aPeak[i].m_Alpha, aRest[i].m_Alpha);
		EXPECT_GT(aPeak[i].m_Spread, aRest[i].m_Spread);
	}
}

// 圆头端帽的"脱开"过渡：缺口没超过两枚端帽时，环看起来仍是闭合的；
// 之后缺口随进度单调张开，不会一出现就切出一道方口。
TEST(QmHookCountdownRender, RoundCapsKeepTheRingClosedUntilTheGapOpens)
{
	EXPECT_FLOAT_EQ(qm_hook_countdown_ui::CountdownVisibleGap(1.0f), 0.0f);
	EXPECT_FLOAT_EQ(qm_hook_countdown_ui::CountdownVisibleGap(0.99f), 0.0f);
	EXPECT_GT(qm_hook_countdown_ui::CountdownVisibleGap(0.5f), 0.0f);
	float Previous = 0.0f;
	for(int Percent = 100; Percent >= 0; --Percent)
	{
		const float Gap = qm_hook_countdown_ui::CountdownVisibleGap(static_cast<float>(Percent) / 100.0f);
		EXPECT_GE(Gap, Previous);
		Previous = Gap;
	}
	// 端帽角度只由内外径之比决定，尺寸档位放大时两端不会张开得更早。
	EXPECT_NEAR(qm_hook_countdown_ui::CountdownCapAngle(), (qm_hook_countdown_ui::OUTER_RADIUS - qm_hook_countdown_ui::INNER_RADIUS) * 0.5f / ((qm_hook_countdown_ui::INNER_RADIUS + qm_hook_countdown_ui::OUTER_RADIUS) * 0.5f), 1e-6f);
}

// 弧带必须保持完整的一段：从环顶一直铺到终点，不能为端帽让开角度。
// 让开的角度正好等于端帽在中径张开的角，环身会在端头前掐成一段细颈，
// 端帽再大也盖不住那道缺口——这就是上一版圆角变形的成因。
TEST(QmHookCountdownRender, ProgressArcKeepsTheFullBandAndPutsCapsOnTheEndpoints)
{
	const float Inner = qm_hook_countdown_ui::INNER_RADIUS;
	const float Outer = qm_hook_countdown_ui::OUTER_RADIUS;
	const float Mid = (Inner + Outer) * 0.5f;
	for(const float Progress : {0.05f, 0.3f, 0.7f, 0.9f})
	{
		SCOPED_TRACE(Progress);
		const auto Plan = qm_hook_countdown_ui::CountdownArcPlan(Progress, Inner, Outer);
		ASSERT_FALSE(Plan.m_FullRing);
		// 弧带扫过的角度就是进度本身，没有被端帽角度吃掉的让开量。
		EXPECT_NEAR(Plan.m_Sweep, 2.0f * pi * Progress, 1e-5f);
		// 端帽半圆正好是环宽的一半，圆心落在标称端点的中径上。
		EXPECT_NEAR(Plan.m_CapRadius, (Outer - Inner) * 0.5f, 1e-5f);
		const qm_hook_countdown_ui::SQmHookCountdownCap &Start = Plan.m_aCaps[0];
		const qm_hook_countdown_ui::SQmHookCountdownCap &End = Plan.m_aCaps[1];
		EXPECT_NEAR(length(Start.m_Center), Mid, 1e-4f);
		EXPECT_NEAR(length(End.m_Center), Mid, 1e-4f);
		EXPECT_NEAR(Start.m_Center.x, 0.0f, 1e-4f);
		EXPECT_NEAR(Start.m_Center.y, -Mid, 1e-4f);
		EXPECT_NEAR(End.m_Center.x, std::sin(Plan.m_Sweep) * Mid, 1e-4f);
		EXPECT_NEAR(End.m_Center.y, -std::cos(Plan.m_Sweep) * Mid, 1e-4f);
		// 半圆朝缺口张开：起点朝角度减小（左）、终点朝前进方向；中线与弧带相切，
		// 平边落在标称端点的半径线上。
		EXPECT_NEAR(std::cos(Start.m_Facing), -1.0f, 1e-5f);
		EXPECT_NEAR(std::sin(Start.m_Facing), 0.0f, 1e-5f);
		EXPECT_NEAR(std::cos(End.m_Facing), std::cos(Plan.m_Sweep), 1e-5f);
		EXPECT_NEAR(std::sin(End.m_Facing), std::sin(Plan.m_Sweep), 1e-5f);
	}
}

// 缺口被端帽盖住后不再画端帽，直接整环收口；进度耗尽时两枚半圆背靠背，
// 合起来就是一个圆点，不会露出扇形缺口，也不需要额外的“合并端帽”分支。
TEST(QmHookCountdownRender, ProgressArcCapsCoverTheGapUntilTheRingCloses)
{
	const float Inner = qm_hook_countdown_ui::INNER_RADIUS;
	const float Outer = qm_hook_countdown_ui::OUTER_RADIUS;
	EXPECT_TRUE(qm_hook_countdown_ui::CountdownArcPlan(1.0f, Inner, Outer).m_FullRing);
	const float Closing = 1.0f - qm_hook_countdown_ui::CountdownCapAngle() / pi;
	EXPECT_TRUE(qm_hook_countdown_ui::CountdownArcPlan(Closing + 0.01f, Inner, Outer).m_FullRing);
	EXPECT_FALSE(qm_hook_countdown_ui::CountdownArcPlan(Closing - 0.05f, Inner, Outer).m_FullRing);
	// 进度 0 什么都不画。
	const auto None = qm_hook_countdown_ui::CountdownArcPlan(0.0f, Inner, Outer);
	EXPECT_FALSE(None.m_FullRing);
	EXPECT_LE(None.m_Sweep, 0.0f);
	// 收尾：两枚半圆几乎重合、朝向相反，合起来就是一个完整的圆点。
	const auto Sliver = qm_hook_countdown_ui::CountdownArcPlan(0.002f, Inner, Outer);
	ASSERT_FALSE(Sliver.m_FullRing);
	EXPECT_LT(length(Sliver.m_aCaps[0].m_Center - Sliver.m_aCaps[1].m_Center), 0.2f);
	EXPECT_NEAR(std::abs(Sliver.m_aCaps[1].m_Facing - Sliver.m_aCaps[0].m_Facing), pi, 0.05f);
	EXPECT_LT(length(Sliver.m_aCaps[0].m_Center), (qm_hook_countdown_ui::INNER_RADIUS + qm_hook_countdown_ui::OUTER_RADIUS) * 0.5f + 0.01f);
}
// HUD 用它决定屏幕外裁剪；必须比实体更大（覆盖光晕）并随尺寸一起放大，
// 否则把尺寸调到 200% 后光晕外圈会被提前裁成一条硬边。
TEST(QmHookCountdownRender, ClipRadiusCoversTheGlowAndFollowsTheSizePercent)
{
	SQmHookCountdownVisual Visual;
	Visual.m_Scale = 1.0f;
	Visual.m_EndlessHook = false;
	const int SavedSize = g_Config.m_QmHookCountdownSize;
	g_Config.m_QmHookCountdownSize = 100;
	const float NormalRing = qm_hook_countdown_ui::RenderClipRadius(Visual);
	EXPECT_GT(NormalRing, qm_hook_countdown_ui::OUTER_RADIUS);
	EXPECT_GE(NormalRing, qm_hook_countdown_ui::OUTER_RADIUS + qm_hook_countdown_ui::GLOW_EXTENT);
	g_Config.m_QmHookCountdownSize = 200;
	EXPECT_NEAR(qm_hook_countdown_ui::RenderClipRadius(Visual), NormalRing * 2.0f, 0.0001f);
	g_Config.m_QmHookCountdownSize = 100;
	// ∞ 的横向半宽更大，裁剪半径必须跟着用 ∞ 的包络。
	Visual.m_EndlessHook = true;
	const float InfinityRadius = qm_hook_countdown_ui::RenderClipRadius(Visual);
	EXPECT_GE(InfinityRadius, QM_HOOK_COUNTDOWN_INFINITY_HALF_WIDTH + qm_hook_countdown_ui::GLOW_EXTENT);
	g_Config.m_QmHookCountdownSize = SavedSize;
}

TEST(QmHudHookCountdown, ReleaseKeepsBreathingUntilBothPhasesAreCleared)
{
	CQmHookCountdown Ring;
	auto Input = PlayerHook();
	Input.m_EndlessHook = true;
	Ring.Update(Input, 0.2f);
	const float BeforeRelease = Ring.Visual().m_BreathPhase;
	Input.m_HookAttached = false;
	Ring.Update(Input, 0.05f);
	EXPECT_GT(Ring.Visual().m_BreathPhase, BeforeRelease);
	Ring.Update(Input, 0.2f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_Alpha, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_FlowPhase, 0.0f);
	EXPECT_FLOAT_EQ(Ring.Visual().m_BreathPhase, 0.0f);
}
