#include <generated/protocol7.h>

#include <game/client/components/qmclient/tee_hue_cycle.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
	constexpr float HUE_EPSILON = 0.001f;
	constexpr float COLOR_EPSILON = 0.0001f;

	ColorRGBA MakeColor(float Hue, float Saturation, float Lightness, float Alpha)
	{
		return color_cast<ColorRGBA>(ColorHSLA(Hue, Saturation, Lightness, Alpha));
	}

	CTeeRenderInfo MakeCustomTeeInfo()
	{
		CTeeRenderInfo Info;
		Info.m_CustomColoredSkin = true;
		Info.m_ColorBody = MakeColor(0.20f, 0.60f, 0.40f, 0.75f);
		Info.m_ColorFeet = MakeColor(0.55f, 0.35f, 0.70f, 0.50f);
		return Info;
	}

	SQmTeeHueCycleConfig MakeEnabledConfig(double TimeSeconds, int SpeedDegreesPerSecond)
	{
		SQmTeeHueCycleConfig Config;
		Config.m_Enabled = true;
		Config.m_PlayerUsesCustomColors = true;
		Config.m_SpeedDegreesPerSecond = SpeedDegreesPerSecond;
		Config.m_TimeSeconds = TimeSeconds;
		return Config;
	}

	void ExpectColorNear(ColorRGBA Actual, ColorRGBA Expected)
	{
		EXPECT_NEAR(Actual.r, Expected.r, COLOR_EPSILON);
		EXPECT_NEAR(Actual.g, Expected.g, COLOR_EPSILON);
		EXPECT_NEAR(Actual.b, Expected.b, COLOR_EPSILON);
		EXPECT_NEAR(Actual.a, Expected.a, COLOR_EPSILON);
	}

	void ExpectHueNear(ColorRGBA Color, float Hue)
	{
		const ColorHSLA Hsla = color_cast<ColorHSLA>(Color);
		EXPECT_NEAR(std::fmod(Hsla.h, 1.0f), Hue, HUE_EPSILON);
	}

	void ExpectSameSla(ColorRGBA Actual, ColorRGBA Original)
	{
		const ColorHSLA ActualHsla = color_cast<ColorHSLA>(Actual);
		const ColorHSLA OriginalHsla = color_cast<ColorHSLA>(Original);
		EXPECT_NEAR(ActualHsla.s, OriginalHsla.s, COLOR_EPSILON);
		EXPECT_NEAR(ActualHsla.l, OriginalHsla.l, COLOR_EPSILON);
		EXPECT_NEAR(ActualHsla.a, OriginalHsla.a, COLOR_EPSILON);
	}
} // namespace

TEST(QmTeeHueCycle, DisabledKeepsInputColors)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	const ColorRGBA OriginalBody = Info.m_ColorBody;
	const ColorRGBA OriginalFeet = Info.m_ColorFeet;
	SQmTeeHueCycleConfig Config = MakeEnabledConfig(10.0, 180);
	Config.m_Enabled = false;

	EXPECT_FALSE(QmApplyTeeHueCycle(Info, Config));
	ExpectColorNear(Info.m_ColorBody, OriginalBody);
	ExpectColorNear(Info.m_ColorFeet, OriginalFeet);
}

TEST(QmTeeHueCycle, BodyAndFeetAdvanceBySamePhase)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	const SQmTeeHueCycleConfig Config = MakeEnabledConfig(0.5, 72);

	EXPECT_TRUE(QmApplyTeeHueCycle(Info, Config));
	ExpectHueNear(Info.m_ColorBody, 0.30f);
	ExpectHueNear(Info.m_ColorFeet, 0.65f);
}

TEST(QmTeeHueCycle, KeepsSaturationLightnessAndAlphaSeparate)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	const ColorRGBA OriginalBody = Info.m_ColorBody;
	const ColorRGBA OriginalFeet = Info.m_ColorFeet;

	EXPECT_TRUE(QmApplyTeeHueCycle(Info, MakeEnabledConfig(1.0, 90)));
	ExpectSameSla(Info.m_ColorBody, OriginalBody);
	ExpectSameSla(Info.m_ColorFeet, OriginalFeet);

	const ColorHSLA Body = color_cast<ColorHSLA>(Info.m_ColorBody);
	const ColorHSLA Feet = color_cast<ColorHSLA>(Info.m_ColorFeet);
	EXPECT_NEAR(Body.s, 0.60f, COLOR_EPSILON);
	EXPECT_NEAR(Feet.s, 0.35f, COLOR_EPSILON);
	EXPECT_NEAR(Body.l, 0.40f, COLOR_EPSILON);
	EXPECT_NEAR(Feet.l, 0.70f, COLOR_EPSILON);
	EXPECT_NEAR(Body.a, 0.75f, COLOR_EPSILON);
	EXPECT_NEAR(Feet.a, 0.50f, COLOR_EPSILON);
}

TEST(QmTeeHueCycle, WrapsHue)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	Info.m_ColorBody = MakeColor(0.98f, 0.60f, 0.40f, 1.0f);

	EXPECT_TRUE(QmApplyTeeHueCycle(Info, MakeEnabledConfig(0.5, 72)));
	ExpectHueNear(Info.m_ColorBody, 0.08f);
}

TEST(QmTeeHueCycle, ZeroSpeedIsStable)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	const ColorRGBA OriginalBody = Info.m_ColorBody;
	const ColorRGBA OriginalFeet = Info.m_ColorFeet;

	EXPECT_TRUE(QmApplyTeeHueCycle(Info, MakeEnabledConfig(999.0, 0)));
	ExpectColorNear(Info.m_ColorBody, OriginalBody);
	ExpectColorNear(Info.m_ColorFeet, OriginalFeet);
}

TEST(QmTeeHueCycle, NonCustomColorsDoNotApply)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	const ColorRGBA OriginalBody = Info.m_ColorBody;
	SQmTeeHueCycleConfig Config = MakeEnabledConfig(1.0, 180);
	Config.m_PlayerUsesCustomColors = false;

	EXPECT_FALSE(QmApplyTeeHueCycle(Info, Config));
	ExpectColorNear(Info.m_ColorBody, OriginalBody);
}

TEST(QmTeeHueCycle, TClientRainbowTeesTakePriority)
{
	CTeeRenderInfo Info = MakeCustomTeeInfo();
	const ColorRGBA OriginalBody = Info.m_ColorBody;
	SQmTeeHueCycleConfig Config = MakeEnabledConfig(1.0, 180);
	Config.m_TClientRainbowTees = true;

	EXPECT_FALSE(QmApplyTeeHueCycle(Info, Config));
	ExpectColorNear(Info.m_ColorBody, OriginalBody);
}

TEST(QmTeeHueCycle, SameAbsoluteTimeIsDeterministic)
{
	CTeeRenderInfo First = MakeCustomTeeInfo();
	CTeeRenderInfo Second = MakeCustomTeeInfo();
	const SQmTeeHueCycleConfig Config = MakeEnabledConfig(12.75, 144);

	EXPECT_TRUE(QmApplyTeeHueCycle(First, Config));
	EXPECT_TRUE(QmApplyTeeHueCycle(Second, Config));
	ExpectColorNear(First.m_ColorBody, Second.m_ColorBody);
	ExpectColorNear(First.m_ColorFeet, Second.m_ColorFeet);
}

TEST(QmTeeHueCycle, PhaseUsesAbsoluteTimeIndependentOfFrameSteps)
{
	const double AbsoluteTimeSeconds = 2.5;
	const double SixtyFpsTimeSeconds = 150.0 * (1.0 / 60.0);
	const double ThirtyFpsTimeSeconds = 75.0 * (1.0 / 30.0);

	EXPECT_NEAR(QmTeeHueCyclePhase(AbsoluteTimeSeconds, 72), 0.5f, HUE_EPSILON);
	EXPECT_NEAR(QmTeeHueCyclePhase(SixtyFpsTimeSeconds, 72), QmTeeHueCyclePhase(AbsoluteTimeSeconds, 72), HUE_EPSILON);
	EXPECT_NEAR(QmTeeHueCyclePhase(ThirtyFpsTimeSeconds, 72), QmTeeHueCyclePhase(AbsoluteTimeSeconds, 72), HUE_EPSILON);
}

TEST(QmTeeHueCycle, AppliesToCustomSevenBodyAndFeetOnly)
{
	CTeeRenderInfo Info;
	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_FEET] = true;
	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_HANDS] = true;
	Info.m_aSixup[0].m_aColors[protocol7::SKINPART_BODY] = MakeColor(0.10f, 0.50f, 0.50f, 0.80f);
	Info.m_aSixup[0].m_aColors[protocol7::SKINPART_FEET] = MakeColor(0.40f, 0.30f, 0.65f, 0.70f);
	Info.m_aSixup[0].m_aColors[protocol7::SKINPART_HANDS] = MakeColor(0.80f, 0.30f, 0.65f, 0.70f);
	const ColorRGBA OriginalHands = Info.m_aSixup[0].m_aColors[protocol7::SKINPART_HANDS];

	EXPECT_TRUE(QmApplyTeeHueCycle(Info, MakeEnabledConfig(0.5, 72)));
	ExpectHueNear(Info.m_aSixup[0].m_aColors[protocol7::SKINPART_BODY], 0.20f);
	ExpectHueNear(Info.m_aSixup[0].m_aColors[protocol7::SKINPART_FEET], 0.50f);
	ExpectColorNear(Info.m_aSixup[0].m_aColors[protocol7::SKINPART_HANDS], OriginalHands);
}

TEST(QmTeeHueCycle, LocalEligibilityKeepsMainTeamplaySemanticsAndGatesDummySeparately)
{
	SQmLocalTeeHueCycleEligibility Main;
	Main.m_IsLocal = true;
	Main.m_UseCustomColors = true;
	EXPECT_TRUE(QmShouldApplyLocalTeeHueCycle(Main));

	SQmLocalTeeHueCycleEligibility MainSeven = Main;
	MainSeven.m_UseCustomColors = false;
	MainSeven.m_UseCustomColors7 = true;
	EXPECT_TRUE(QmShouldApplyLocalTeeHueCycle(MainSeven));

	SQmLocalTeeHueCycleEligibility Dummy = Main;
	Dummy.m_IsDummy = true;
	EXPECT_FALSE(QmShouldApplyLocalTeeHueCycle(Dummy));
	Dummy.m_DummyEnabled = true;
	EXPECT_TRUE(QmShouldApplyLocalTeeHueCycle(Dummy));

	Dummy.m_UseCustomColors = false;
	Dummy.m_UseCustomColors7 = false;
	EXPECT_FALSE(QmShouldApplyLocalTeeHueCycle(Dummy));

	Dummy.m_IsLocal = false;
	Dummy.m_UseCustomColors = true;
	EXPECT_FALSE(QmShouldApplyLocalTeeHueCycle(Dummy));
}
