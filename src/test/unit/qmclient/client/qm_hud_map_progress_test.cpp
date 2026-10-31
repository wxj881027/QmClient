#include <game/client/components/hud_media_island_logic.h>
#include <game/client/components/qmclient/hud_map_progress.h>

#include <gtest/gtest.h>

#include <array>

using QmHudMapProgress::ESurface;

TEST(QmHudMapProgressSurface, DisabledProgressDoesNotReserveAnySurface)
{
	for(int Style = 0; Style <= 2; ++Style)
	{
		SCOPED_TRACE(Style);
		EXPECT_EQ(QmHudMapProgress::ResolveSurface(false, Style, true, true), ESurface::HIDDEN);
	}
}

TEST(QmHudMapProgressSurface, IslandSelectionIsIndependentOfPlayerStatistics)
{
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 2, false, true), ESurface::ISLAND);
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 2, true, true), ESurface::ISLAND);
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 0, true, true), ESurface::STANDALONE);
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 1, true, true), ESurface::PLAYER_STATS);
}

TEST(QmHudMapProgressSurface, UnavailableSelectedContainerFallsBackToStandalone)
{
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 2, true, false), ESurface::STANDALONE);
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 1, false, true), ESurface::STANDALONE);
	EXPECT_EQ(QmHudMapProgress::ResolveSurface(true, 2, true, true), ESurface::ISLAND);
}

TEST(QmHudMapProgressRows, LyricsExpansionKeepsProgressAtTheSameHeight)
{
	const auto Compact = QmHudMapProgress::IslandRows(true, true, 0, 16.0f, 7.0f, 2.5f);
	const auto Expanded = QmHudMapProgress::IslandRows(true, true, 2, 16.0f, 7.0f, 2.5f);
	EXPECT_FLOAT_EQ(Compact.m_ProgressY, Expanded.m_ProgressY);
	EXPECT_FLOAT_EQ(Compact.m_TextY, Expanded.m_TextY);
	EXPECT_FLOAT_EQ(Expanded.m_TextY - Expanded.m_ProgressY, 7.0f);
	EXPECT_FLOAT_EQ(Expanded.m_BottomHeight - Compact.m_BottomHeight, 14.0f);
}

TEST(QmHudMapProgressRows, ProgressWithoutOtherHudContentUsesTheCompactHeader)
{
	const auto Rows = QmHudMapProgress::IslandRows(false, true, 0, 16.0f, 7.0f, 2.5f);
	EXPECT_TRUE(Rows.m_ProgressInHeader);
	EXPECT_FLOAT_EQ(Rows.m_ProgressY + 3.5f, 8.0f);
	EXPECT_FLOAT_EQ(Rows.m_BottomHeight, 0.0f);
}

TEST(QmHudMapProgressRows, SwapRequestsAndTranslatedLyricsStayBelowProgress)
{
	const auto SwapRows = QmHudMediaIslandSwapRows(3, true, true, 2);
	const auto Rows = QmHudMapProgress::IslandRows(true, true, SwapRows.m_BottomLineCount, 16.0f, 7.0f, 2.5f);
	EXPECT_GE(Rows.m_TextY, Rows.m_ProgressY + 7.0f);
	const float LyricsY = Rows.m_TextY + SwapRows.m_LyricsLineIndex * 7.0f;
	EXPECT_GE(LyricsY, Rows.m_TextY + SwapRows.m_BottomSwapCount * 7.0f);
	EXPECT_FLOAT_EQ(LyricsY + 14.0f + 2.5f, 16.0f + Rows.m_BottomHeight);
}

TEST(QmHudMapProgressRows, DisablingProgressReleasesItsRowAndKeepsExistingTextSpacing)
{
	const auto WithProgress = QmHudMapProgress::IslandRows(true, true, 1, 16.0f, 7.0f, 2.5f);
	const auto WithoutProgress = QmHudMapProgress::IslandRows(true, false, 1, 16.0f, 7.0f, 2.5f);
	EXPECT_FLOAT_EQ(WithProgress.m_BottomHeight - WithoutProgress.m_BottomHeight, 7.0f);
	EXPECT_FLOAT_EQ(WithoutProgress.m_TextY, 18.5f);
	EXPECT_FLOAT_EQ(QmHudMapProgress::IslandRows(true, false, 0, 16.0f, 7.0f, 2.5f).m_BottomHeight, 0.0f);
}

TEST(QmHudMapProgressLayout, ReservedValueAndLabelLeaveAUsableTrackAtMinimumWidth)
{
	for(float Scale : {0.7f, 1.0f, 1.5f})
	{
		SCOPED_TRACE(Scale);
		const float LabelWidth = 12.0f * Scale;
		const float ValueWidth = 18.0f * Scale;
		const CUIRect Row{30.0f, 20.0f, QmHudMapProgress::MinimumWidth(LabelWidth, ValueWidth, Scale), 7.0f * Scale};
		const auto Layout = QmHudMapProgress::StripLayout(Row, LabelWidth, ValueWidth, Scale);
		ASSERT_TRUE(Layout.m_Visible);
		EXPECT_GE(Layout.m_Label.x, Row.x);
		EXPECT_GT(Layout.m_Track.x, Layout.m_Label.x + Layout.m_Label.w);
		EXPECT_NEAR(Layout.m_Track.w, 16.0f * Scale, 0.0001f);
		EXPECT_LT(Layout.m_Track.x + Layout.m_Track.w, Layout.m_Value.x);
		EXPECT_LE(Layout.m_Value.x + Layout.m_Value.w, Row.x + Row.w);
		EXPECT_GE(Layout.m_Value.y, Row.y);
		EXPECT_LE(Layout.m_Value.y + Layout.m_Value.h, Row.y + Row.h);
	}
}

TEST(QmHudMapProgressLayout, EnteringCapsuleDoesNotDrawTextOutsideItsAvailableSpace)
{
	const float Width = QmHudMapProgress::MinimumWidth(12.0f, 18.0f, 1.0f);
	EXPECT_FALSE(QmHudMapProgress::StripLayout({0.0f, 0.0f, Width - 1.0f, 7.0f}, 12.0f, 18.0f, 1.0f).m_Visible);
	EXPECT_FALSE(QmHudMapProgress::StripLayout({0.0f, 0.0f, Width, 3.0f}, 12.0f, 18.0f, 1.0f).m_Visible);
}

TEST(QmHudMapProgressState, ProgressCanMoveBackwardsAndClampsAtMapBounds)
{
	QmHudMapProgress::CProgressState State;
	EXPECT_FLOAT_EQ(State.Update(true, 0.8f, 0.0f), 0.8f);
	const float Backwards = State.Update(true, 0.2f, 0.05f);
	EXPECT_GT(Backwards, 0.2f);
	EXPECT_LT(Backwards, 0.8f);
	EXPECT_FLOAT_EQ(State.Update(true, 2.0f, 1.0f), 1.0f);
	EXPECT_FLOAT_EQ(State.Update(true, -1.0f, 1.0f), 0.0f);
}

TEST(QmHudMapProgressState, MissingProgressAndResetDiscardThePreviousRun)
{
	QmHudMapProgress::CProgressState State;
	State.Update(true, 0.9f, 0.0f);
	EXPECT_FLOAT_EQ(State.Update(false, 0.9f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(State.Update(true, 0.1f, 0.0f), 0.1f);
	State.Reset();
	EXPECT_FLOAT_EQ(State.Update(true, 0.6f, 0.0f), 0.6f);
}

TEST(QmHudMapProgressState, DummyAndMainCharacterKeepIndependentDisplayedProgress)
{
	std::array<QmHudMapProgress::CProgressState, 2> aState;
	EXPECT_FLOAT_EQ(aState[0].Update(true, 0.2f, 0.0f), 0.2f);
	EXPECT_FLOAT_EQ(aState[1].Update(true, 0.8f, 0.0f), 0.8f);
	EXPECT_FLOAT_EQ(aState[0].Update(true, 0.2f, 0.0f), 0.2f);
}
