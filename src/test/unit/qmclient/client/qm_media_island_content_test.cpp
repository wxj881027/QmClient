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

TEST(QmHudMediaIslandLyrics, ActiveLyricsKeepTheIslandExpandedWithoutRepeatedMorphs)
{
	SHudMediaIslandExpansionState State;
	State = QmHudMediaIslandUpdateExpansion(State, true, true, false, 1000, 3000);
	EXPECT_TRUE(State.m_Expanded);
	EXPECT_TRUE(State.m_LyricsActive);
	EXPECT_EQ(State.m_ExpandUntilTick, 0);
	EXPECT_TRUE(State.m_StartCapsuleMorph);

	State.m_StartCapsuleMorph = false;
	State = QmHudMediaIslandUpdateExpansion(State, true, true, false, 100000, 3000);
	EXPECT_TRUE(State.m_Expanded);
	EXPECT_EQ(State.m_ExpandUntilTick, 0);
	EXPECT_FALSE(State.m_StartCapsuleMorph);
}

TEST(QmHudMediaIslandLyrics, TrackDetailsUseTheDeadlineUnlessAlwaysShown)
{
	EXPECT_TRUE(QmHudMediaIslandShouldShowTrackDetails(1000, 4000, false));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTrackDetails(4000, 4000, false));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTrackDetails(5000, 0, false));
	EXPECT_TRUE(QmHudMediaIslandShouldShowTrackDetails(4000, 4000, true));
	EXPECT_TRUE(QmHudMediaIslandShouldShowTrackDetails(5000, 0, true));
}

TEST(QmHudMediaIslandLyrics, LosingLyricsRestoresTheNormalAutoCollapseDeadline)
{
	SHudMediaIslandExpansionState State;
	State.m_Expanded = true;
	State.m_LyricsActive = true;
	State = QmHudMediaIslandUpdateExpansion(State, true, false, false, 1000, 3000);
	EXPECT_TRUE(State.m_Expanded);
	EXPECT_EQ(State.m_ExpandUntilTick, 4000);
	EXPECT_FALSE(State.m_StartCapsuleMorph);

	State = QmHudMediaIslandUpdateExpansion(State, true, false, false, 4000, 3000);
	EXPECT_FALSE(State.m_Expanded);
	EXPECT_EQ(State.m_ExpandUntilTick, 0);
	EXPECT_TRUE(State.m_StartCapsuleMorph);
}

TEST(QmHudMediaIslandLyrics, ShortLyricsNeverScroll)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(80.0f, 100.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(100.0f, 100.0f, 500.0f), 0.0f);
}

TEST(QmHudMediaIslandLyrics, LongLyricsPauseTravelAndReturnWithinTheViewport)
{
	constexpr float TextWidth = 200.0f;
	constexpr float ViewportWidth = 100.0f;
	constexpr float Speed = 50.0f;
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 0.0f, Speed), 0.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 1.2f, Speed), 0.0f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 2.2f, Speed), 50.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 3.2f, Speed), 100.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 4.2f, Speed), 100.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 5.2f, Speed), 50.0f, 0.001f);
	EXPECT_NEAR(QmHudMediaIslandMarqueeOffset(TextWidth, ViewportWidth, 6.2f, Speed), 0.0f, 0.001f);
}

TEST(QmHudMediaIslandLyrics, NewLineResetsTheMarqueeAtItsStartingPause)
{
	EXPECT_FALSE(QmHudMediaIslandShouldResetMarquee("same", "same"));
	EXPECT_TRUE(QmHudMediaIslandShouldResetMarquee("first", "second"));
	EXPECT_TRUE(QmHudMediaIslandShouldResetMarquee(nullptr, "line"));
	EXPECT_FLOAT_EQ(QmHudMediaIslandMarqueeOffset(200.0f, 100.0f, 0.0f), 0.0f);
}

TEST(QmHudMediaIslandRecording, AlphaBreathRepeatsEveryTwoPointFourSeconds)
{
	EXPECT_NEAR(QmHudRecordingDotAlpha(0.0), 0.95f, 0.00001f);
	EXPECT_NEAR(QmHudRecordingDotAlpha(0.6), 0.80f, 0.00001f);
	EXPECT_NEAR(QmHudRecordingDotAlpha(1.2), 0.65f, 0.00001f);
	EXPECT_NEAR(QmHudRecordingDotAlpha(1.8), 0.80f, 0.00001f);
	EXPECT_NEAR(QmHudRecordingDotAlpha(2.4), 0.95f, 0.00001f);
}

TEST(QmHudMediaIslandLyrics, EachSupportedHookEnablesUnifiedLyrics)
{
	EXPECT_FALSE(QmHudMusicLyricsSourceEnabled(false, false, false));
	EXPECT_TRUE(QmHudMusicLyricsSourceEnabled(true, false, false));
	EXPECT_TRUE(QmHudMusicLyricsSourceEnabled(false, true, false));
	EXPECT_TRUE(QmHudMusicLyricsSourceEnabled(false, false, true));
}

TEST(QmHudMediaIslandRecording, SdfDotIsAPlainCircleWithPixelFeather)
{
	const SHudMediaIslandSdfRenderState State = QmHudRecordingDotSdfState(vec2(20.0f, 30.0f), 6.0f, 0.7f, 0.5f);
	EXPECT_FLOAT_EQ(State.m_MainRect.x, 17.0f);
	EXPECT_FLOAT_EQ(State.m_MainRect.y, 27.0f);
	EXPECT_FLOAT_EQ(State.m_MainRect.w, 6.0f);
	EXPECT_FLOAT_EQ(State.m_MainRect.h, 6.0f);
	EXPECT_FLOAT_EQ(State.m_MainRadius, 3.0f);
	EXPECT_EQ(State.m_MainCorners, IGraphics::CORNER_ALL);
	EXPECT_FLOAT_EQ(State.m_BackgroundColor.r, 1.0f);
	EXPECT_FLOAT_EQ(State.m_BackgroundColor.g, 0.15f);
	EXPECT_FLOAT_EQ(State.m_BackgroundColor.b, 0.15f);
	EXPECT_FLOAT_EQ(State.m_BackgroundColor.a, 0.7f);
	EXPECT_EQ(State.m_ItemCount, 0);
	EXPECT_FALSE(State.m_HasRightCapsule);
	EXPECT_FLOAT_EQ(State.m_OuterShadowSize, 0.0f);
	EXPECT_FLOAT_EQ(State.m_OuterShadowOpacity, 0.0f);
	EXPECT_FLOAT_EQ(State.m_BackdropUv.z, 0.0f);
	EXPECT_LT(State.m_Rect.x, State.m_MainRect.x);

	IGraphics::SMediaIslandSdfParams Params;
	ASSERT_TRUE(QmHudMediaIslandBuildGpuSdfParams(State, Params));
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_MAIN_PARAMS].x, 3.0f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_METADATA].w, 0.5f);
	EXPECT_FLOAT_EQ(Params.m_aData[IGraphics::SMediaIslandSdfParams::DATA_BACKGROUND].w, 0.7f);
}

TEST(QmHudMediaIslandWaveform, PlayingBarsVaryIndependentlyAndPausedBarsSettle)
{
	bool AnyChanged = false;
	for(int Bar = 0; Bar < 7; ++Bar)
	{
		const float First = QmHudMediaIslandWaveBarHeight(Bar, 1.0f, 1.0f);
		const float Next = QmHudMediaIslandWaveBarHeight(Bar, 1.1f, 1.0f);
		EXPECT_GE(First, 0.20f);
		EXPECT_LE(First, 1.0f);
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarHeight(Bar, 1.0f, 0.0f), 0.20f);
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarHeight(Bar, 1.0f, 0.5f), 0.20f + (First - 0.20f) * 0.5f);
		AnyChanged |= std::abs(First - Next) > 0.001f;
	}
	EXPECT_TRUE(AnyChanged);

	constexpr int SampleCount = 80;
	constexpr float SampleStep = 0.125f;
	for(int FirstBar = 0; FirstBar < 7; ++FirstBar)
	{
		for(int SecondBar = FirstBar + 1; SecondBar < 7; ++SecondBar)
		{
			double FirstSum = 0.0;
			double SecondSum = 0.0;
			double FirstSquaredSum = 0.0;
			double SecondSquaredSum = 0.0;
			double ProductSum = 0.0;
			for(int Sample = 0; Sample < SampleCount; ++Sample)
			{
				const float Time = Sample * SampleStep;
				const double First = QmHudMediaIslandWaveBarHeight(FirstBar, Time, 1.0f);
				const double Second = QmHudMediaIslandWaveBarHeight(SecondBar, Time, 1.0f);
				FirstSum += First;
				SecondSum += Second;
				FirstSquaredSum += First * First;
				SecondSquaredSum += Second * Second;
				ProductSum += First * Second;
			}

			const double Numerator = SampleCount * ProductSum - FirstSum * SecondSum;
			const double Denominator = std::sqrt(
				(SampleCount * FirstSquaredSum - FirstSum * FirstSum) *
				(SampleCount * SecondSquaredSum - SecondSum * SecondSum));
			ASSERT_GT(Denominator, 0.0);
			EXPECT_LT(std::abs(Numerator / Denominator), 0.20) << "bars " << FirstBar << " and " << SecondBar;
		}
	}
}

TEST(QmHudMediaIslandWaveform, StoppedBarsSettleFromOutsideIn)
{
	constexpr int BarCount = 7;
	constexpr float MidSettleTime = 0.40f;
	const float Outer = QmHudMediaIslandWaveBarSettleProgress(0, BarCount, MidSettleTime);
	const float NextOuter = QmHudMediaIslandWaveBarSettleProgress(1, BarCount, MidSettleTime);
	const float NextInner = QmHudMediaIslandWaveBarSettleProgress(2, BarCount, MidSettleTime);
	const float Center = QmHudMediaIslandWaveBarSettleProgress(3, BarCount, MidSettleTime);

	EXPECT_GT(Outer, NextOuter);
	EXPECT_GT(NextOuter, NextInner);
	EXPECT_GT(NextInner, Center);
	EXPECT_FLOAT_EQ(Outer, QmHudMediaIslandWaveBarSettleProgress(6, BarCount, MidSettleTime));
	EXPECT_FLOAT_EQ(NextOuter, QmHudMediaIslandWaveBarSettleProgress(5, BarCount, MidSettleTime));
	EXPECT_FLOAT_EQ(NextInner, QmHudMediaIslandWaveBarSettleProgress(4, BarCount, MidSettleTime));
	for(int Bar = 0; Bar < BarCount; ++Bar)
	{
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarSettleProgress(Bar, BarCount, -0.1f), 0.0f);
		EXPECT_FLOAT_EQ(QmHudMediaIslandWaveBarSettleProgress(Bar, BarCount, 0.9f), 1.0f);
	}
}
