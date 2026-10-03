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

TEST(QmHudMediaIslandLayout, CompactHeightIsAboutFortyPixelsAt1080p)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesignScale, 0.7f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(16.0f), 11.2f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(12.0f), 8.4f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(5.8f), 4.06f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandScaled(3.0f), 2.1f);
	EXPECT_NEAR(QmHudMediaIslandScaled(16.0f) * 1080.0f / 300.0f, 40.32f, 0.001f);
}

TEST(QmHudMediaIslandLayout, MainCapsuleWithoutTimerRequiresContentOrCountdownSatellite)
{
	EXPECT_FALSE(QmHudMediaIslandShouldReserveMainCapsule(false, false, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(true, false, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(false, true, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(false, false, true, false));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(true, true, true, false));
}

TEST(QmHudMediaIslandLayout, CountdownUsesTimerWithoutReservingEmptyMainCapsule)
{
	EXPECT_FALSE(QmHudMediaIslandShouldReserveMainCapsule(false, false, true, true));
	EXPECT_FALSE(QmHudMediaIslandShouldReserveMainCapsule(false, false, false, true));
}

TEST(QmHudMediaIslandLayout, CountdownKeepsMediaAndTeamContentBesideTimer)
{
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(true, false, true, true));
	EXPECT_TRUE(QmHudMediaIslandShouldReserveMainCapsule(false, true, true, true));
}

TEST(QmHudMediaIslandLayout, MainSectionExpansionKeepsRaceTimerCentered)
{
	for(const float ScreenWidth : {400.0f, 533.3333f, 700.0f})
	{
		SCOPED_TRACE(ScreenWidth);
		for(const float MainWidth : {0.0f, 11.2f, 35.0f, 80.0f, 140.0f})
		{
			SCOPED_TRACE(MainWidth);
			const float Gap = MainWidth > 0.0f ? QmHudMediaIslandScaled(3.0f) : 0.0f;
			const SHudMediaIslandTimerAnchor Anchor = QmHudMediaIslandAnchorTimer(ScreenWidth * 0.5f, 42.0f, MainWidth, Gap);
			EXPECT_NEAR(Anchor.m_TimerX + 21.0f, ScreenWidth * 0.5f, 0.0001f);
			EXPECT_NEAR(Anchor.m_MainX + MainWidth + Gap, Anchor.m_TimerX, 0.0001f);
		}
	}
}

TEST(QmHudMediaIslandLayout, IncomingSwapWidthKeepsTimerTextOnTheSameCenter)
{
	constexpr float CenterX = 266.5f;
	constexpr float TextWidth = 30.0f;
	for(const float TimerWidth : {42.0f, 80.0f, 160.0f, 42.0f})
	{
		SCOPED_TRACE(TimerWidth);
		const SHudMediaIslandTimerAnchor Anchor = QmHudMediaIslandAnchorTimer(CenterX, TimerWidth, 60.0f, 2.1f);
		const float TextX = Anchor.m_TimerX + (TimerWidth - TextWidth) * 0.5f;
		EXPECT_NEAR(TextX, CenterX - TextWidth * 0.5f, 0.0001f);
	}
}

TEST(QmHudMediaIslandLayout, InfoStackMirrorsRowsAroundHorizontalMidlineWithCompactGap)
{
	constexpr float IslandY = 1.0f;
	constexpr float IslandHeight = QmHudMediaIslandScaled(16.0f);
	constexpr float TextHeight = QmHudMediaIslandScaled(4.4f);
	constexpr float TextGap = QmHudMediaIslandScaled(0.8f);
	const SHudMediaIslandInfoStackLayout Layout = QmHudMediaIslandMirroredInfoStack(IslandY, IslandHeight, TextHeight, TextGap);
	const float MidY = IslandY + IslandHeight * 0.5f;

	EXPECT_FLOAT_EQ(MidY - Layout.m_TopCenterY, Layout.m_BottomCenterY - MidY);
	EXPECT_FLOAT_EQ(Layout.m_BottomCenterY - Layout.m_TopCenterY, QmHudMediaIslandScaled(5.2f));
	EXPECT_NEAR(
		(Layout.m_BottomCenterY - TextHeight * 0.5f) - (Layout.m_TopCenterY + TextHeight * 0.5f),
		TextGap,
		0.0001f);
}

TEST(QmHudMediaIslandLayout, ActiveLyricsKeepAFixedViewportWithAnExistingTopRow)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, true, false, 0.0f, 72.0f, 300.0f, 10.0f), 92.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, true, false, 0.0f, 500.0f, 300.0f, 10.0f), 300.0f);
}

TEST(QmHudMediaIslandLayout, LyricsOnlyUsesFixedTitleAreaWidth)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, false, false, 0.0f, 72.0f, 300.0f, 10.0f), 92.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, false, false, 0.0f, 500.0f, 100.0f, 10.0f), 100.0f);
}

TEST(QmHudMediaIslandLayout, UtilityBottomContentStillControlsRequestedWidth)
{
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(true, true, true, 45.0f, 72.0f, 300.0f, 10.0f), 92.0f);
	EXPECT_FLOAT_EQ(QmHudMediaIslandDesiredBottomWidth(false, false, true, 45.0f, 72.0f, 300.0f, 10.0f), 65.0f);
}

TEST(QmHudMediaIslandLayout, FirstIncomingSwapReplacesCheckpointAndLyricsRemainLast)
{
	const SHudMediaIslandSwapRows Rows = QmHudMediaIslandSwapRows(3, true, true);
	EXPECT_EQ(Rows.m_InlineSwapCount, 1);
	EXPECT_EQ(Rows.m_BottomSwapCount, 2);
	EXPECT_EQ(Rows.m_BottomLineCount, 3);
	EXPECT_EQ(Rows.m_LyricsLineIndex, 2);
}

TEST(QmHudMediaIslandLayout, SwapsUseBottomRowsWhenRaceTimerIsUnavailable)
{
	const SHudMediaIslandSwapRows Rows = QmHudMediaIslandSwapRows(3, false, true);
	EXPECT_EQ(Rows.m_InlineSwapCount, 0);
	EXPECT_EQ(Rows.m_BottomSwapCount, 3);
	EXPECT_EQ(Rows.m_BottomLineCount, 4);
	EXPECT_EQ(Rows.m_LyricsLineIndex, 3);
}

TEST(QmHudMediaIslandTimerLayout, SecondaryLinePreservesTenPercentTopMargin)
{
	const SHudMediaIslandTimerRowLayout Layout = QmHudMediaIslandTimerRows(1.0f, 16.0f, true);

	EXPECT_FLOAT_EQ(Layout.m_RaceY, 2.6f);
	EXPECT_FLOAT_EQ(Layout.m_RaceH, 9.6f);
	EXPECT_FLOAT_EQ(Layout.m_CheckpointY, 12.2f);
	EXPECT_FLOAT_EQ(Layout.m_CheckpointH, 4.8f);
}

TEST(QmHudMediaIslandTimerLayout, RaceKeepsTheSameSlotWhenSecondaryLineIsHidden)
{
	for(const float Scale : {QmHudMediaIslandDesignScale, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		const float BoxY = 1.0f;
		const float BoxH = 16.0f * Scale;
		const SHudMediaIslandTimerRowLayout WithSecondaryLine = QmHudMediaIslandTimerRows(BoxY, BoxH, true);
		const SHudMediaIslandTimerRowLayout WithoutSecondaryLine = QmHudMediaIslandTimerRows(BoxY, BoxH, false);

		EXPECT_FLOAT_EQ(WithoutSecondaryLine.m_RaceY, WithSecondaryLine.m_RaceY);
		EXPECT_FLOAT_EQ(WithoutSecondaryLine.m_RaceH, WithSecondaryLine.m_RaceH);
		EXPECT_FLOAT_EQ(WithoutSecondaryLine.m_CheckpointH, 0.0f);
	}
}

TEST(QmHudMediaIslandLayout, InfoStackMirrorsRowsAroundTopAnchoredHorizontalMidlineWithCompactGap)
{
	constexpr float IslandY = 0.0f;
	constexpr float IslandHeight = QmHudMediaIslandScaled(16.0f);
	constexpr float TextHeight = QmHudMediaIslandScaled(4.4f);
	constexpr float TextGap = QmHudMediaIslandScaled(0.8f);
	const SHudMediaIslandInfoStackLayout Layout = QmHudMediaIslandMirroredInfoStack(IslandY, IslandHeight, TextHeight, TextGap);
	const float MidY = IslandY + IslandHeight * 0.5f;

	EXPECT_FLOAT_EQ(MidY - Layout.m_TopCenterY, Layout.m_BottomCenterY - MidY);
	EXPECT_FLOAT_EQ(Layout.m_BottomCenterY - Layout.m_TopCenterY, QmHudMediaIslandScaled(5.2f));
	EXPECT_NEAR(
		(Layout.m_BottomCenterY - TextHeight * 0.5f) - (Layout.m_TopCenterY + TextHeight * 0.5f),
		TextGap,
		0.0001f);
}
