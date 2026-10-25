#include <game/client/QmUi/QmLegacyMediaHud.h>
#include <game/client/QmUi/QmLyricsHud.h>
#include <game/client/components/hud_media_island_logic.h>

#include <gtest/gtest.h>

TEST(QmLyricsHudLayout, DetachedIslandHasAGapBelowTheMainIsland)
{
	const CUIRect Main{150.0f, 10.0f, 80.0f, 12.0f};
	const auto Lyrics = QmLyricsIslandLayout(Main, 400.0f, 300.0f, 1);
	EXPECT_GT(Lyrics.y, Main.y + Main.h);
	EXPECT_FLOAT_EQ(Lyrics.w, Main.w);
	EXPECT_FLOAT_EQ(Lyrics.x + Lyrics.w * 0.5f, Main.x + Main.w * 0.5f);
}

TEST(QmLyricsHudLayout, DetachedWidthFollowsChangesToTheMainIsland)
{
	for(const float Width : {60.0f, 80.0f, 140.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Main{100.0f, 10.0f, Width, 12.0f};
		const auto Lyrics = QmLyricsIslandLayout(Main, 400.0f, 300.0f, 1);
		EXPECT_FLOAT_EQ(Lyrics.x, Main.x);
		EXPECT_FLOAT_EQ(Lyrics.w, Width);
	}
}

TEST(QmLyricsHudLayout, BilingualRowsIncreaseHeightWithoutChangingWidthOrTop)
{
	const CUIRect Main{150.0f, 10.0f, 80.0f, 12.0f};
	const auto Single = QmLyricsIslandLayout(Main, 400.0f, 300.0f, 1);
	const auto Bilingual = QmLyricsIslandLayout(Main, 400.0f, 300.0f, 2);
	EXPECT_GT(Bilingual.h, Single.h);
	EXPECT_FLOAT_EQ(Bilingual.w, Single.w);
	EXPECT_FLOAT_EQ(Bilingual.y, Single.y);
}

TEST(QmLyricsHudLayout, NarrowViewportKeepsTheWholeIslandOnScreen)
{
	const auto Lyrics = QmLyricsIslandLayout({100.0f, 290.0f, 80.0f, 12.0f}, 80.0f, 300.0f, 2);
	EXPECT_GE(Lyrics.x, 0.0f);
	EXPECT_GE(Lyrics.y, 0.0f);
	EXPECT_LE(Lyrics.x + Lyrics.w, 80.0f);
	EXPECT_LE(Lyrics.y + Lyrics.h, 300.0f);
}

TEST(QmLyricsHudLayout, DefaultAnchorFollowsTheEditedMainIslandTransform)
{
	const auto Anchor = QmLyricsHudAnchor({20.0f, 10.0f, 100.0f, 12.0f},
		{0.0f, 0.0f, 200.0f, 20.0f}, {100.0f, 50.0f, 400.0f, 40.0f});
	EXPECT_FLOAT_EQ(Anchor.x, 140.0f);
	EXPECT_FLOAT_EQ(Anchor.y, 70.0f);
	EXPECT_FLOAT_EQ(Anchor.w, 200.0f);
	EXPECT_FLOAT_EQ(Anchor.h, 24.0f);
	const auto Lyrics = QmLyricsIslandLayout(Anchor, 600.0f, 300.0f, 2);
	EXPECT_FLOAT_EQ(Lyrics.x, Anchor.x);
	EXPECT_FLOAT_EQ(Lyrics.w, Anchor.w);
}

TEST(QmLyricsHudLayout, TwoLyricRowsReserveSpaceAfterIncomingSwapRequests)
{
	const auto Rows = QmHudMediaIslandSwapRows(3, true, true, 2);
	EXPECT_EQ(Rows.m_InlineSwapCount, 1);
	EXPECT_EQ(Rows.m_LyricsLineIndex, 2);
	EXPECT_EQ(Rows.m_BottomLineCount, 4);
}

TEST(QmLyricsHudLayout, LegacyBilingualLyricsKeepMetadataInPlace)
{
	const auto Single = QmLegacyMediaHudLayout(10, 40, 400, 300, QmLegacyMediaHudContent(true, true, true, true, 1));
	const auto Bilingual = QmLegacyMediaHudLayout(10, 40, 400, 300, QmLegacyMediaHudContent(true, true, true, true, 2));
	EXPECT_FLOAT_EQ(Single.m_Cover.y, Bilingual.m_Cover.y);
	EXPECT_FLOAT_EQ(Single.m_Title.y, Bilingual.m_Title.y);
	EXPECT_GT(Bilingual.m_Panel.h, Single.m_Panel.h);
}

TEST(QmLyricsMarquee, TranslationChangesRestartTheScroll)
{
	CQmLyricsMarqueeState State;
	const char *apFirst[] = {"original", "first translation"};
	const char *apSecond[] = {"original", "second translation"};
	EXPECT_FLOAT_EQ(State.Update(apFirst, 2, 1000, 1000), 0.0f);
	EXPECT_FLOAT_EQ(State.Update(apFirst, 2, 1500, 1000), 0.5f);
	EXPECT_FLOAT_EQ(State.Update(apSecond, 2, 1500, 1000), 0.0f);
}

TEST(QmLyricsMarquee, LegacyTranslationChangeRestartsTheScroll)
{
	CQmLegacyMediaHudLyricState State;
	CSystemMediaControls::SState Media{};
	State.Update(Media, "original", 1000, "first");
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(1500, 1000), 0.5f);
	State.Update(Media, "original", 1500, "second");
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(1500, 1000), 0.0f);
}
