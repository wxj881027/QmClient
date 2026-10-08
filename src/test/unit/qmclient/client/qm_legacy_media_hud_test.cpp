#include <game/client/QmUi/QmLegacyMediaHud.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace
{
	void ExpectContained(const CUIRect &Outer, const CUIRect &Inner)
	{
		EXPECT_GT(Inner.w, 0.0f);
		EXPECT_GT(Inner.h, 0.0f);
		EXPECT_GE(Inner.x, Outer.x - 0.0001f);
		EXPECT_GE(Inner.y, Outer.y - 0.0001f);
		EXPECT_LE(Inner.x + Inner.w, Outer.x + Outer.w + 0.0001f);
		EXPECT_LE(Inner.y + Inner.h, Outer.y + Outer.h + 0.0001f);
	}

	void ExpectSeparate(const CUIRect &First, const CUIRect &Second)
	{
		EXPECT_TRUE(First.x + First.w <= Second.x || Second.x + Second.w <= First.x ||
			First.y + First.h <= Second.y || Second.y + Second.h <= First.y);
	}

	CSystemMediaControls::SState Media(const char *pSource = "music-player", const char *pTitle = "Song", const char *pArtist = "Artist")
	{
		CSystemMediaControls::SState State;
		str_copy(State.m_aSourceAppId, pSource, sizeof(State.m_aSourceAppId));
		str_copy(State.m_aTitle, pTitle, sizeof(State.m_aTitle));
		str_copy(State.m_aArtist, pArtist, sizeof(State.m_aArtist));
		return State;
	}
}

TEST(QmLegacyMediaHudContent, NoMetadataOrLyricsHidesThePanel)
{
	const auto Content = QmLegacyMediaHudContent(false, false, false, false);
	EXPECT_FALSE(Content.m_Visible);
	EXPECT_FALSE(Content.m_ShowMetadata);
	EXPECT_FALSE(Content.m_ShowArtist);
	EXPECT_FALSE(Content.m_ShowLyrics);
	const auto Layout = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, Content);
	EXPECT_FLOAT_EQ(Layout.m_Panel.w, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Panel.h, 0.0f);
}

TEST(QmLegacyMediaHudContent, ArtistWithoutTitleUsesTheMainTextRow)
{
	const auto Content = QmLegacyMediaHudContent(false, true, false, false);
	ASSERT_TRUE(Content.m_Visible);
	EXPECT_TRUE(Content.m_ShowMetadata);
	EXPECT_FALSE(Content.m_ShowArtist);
	const auto Layout = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, Content);
	ExpectContained(Layout.m_Panel, Layout.m_Title);
	EXPECT_FLOAT_EQ(Layout.m_Artist.h, 0.0f);
	EXPECT_GT(Layout.m_Progress.w, 0.0f);
}

TEST(QmLegacyMediaHudContent, CurrentLyricAddsAnIndependentRowBelowMetadata)
{
	const auto Content = QmLegacyMediaHudContent(true, true, true, false);
	ASSERT_TRUE(Content.m_ShowMetadata);
	ASSERT_TRUE(Content.m_ShowLyrics);
	const auto Layout = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, Content);
	ExpectContained(Layout.m_Panel, Layout.m_Lyrics);
	EXPECT_GT(Layout.m_Lyrics.y, Layout.m_Progress.y + Layout.m_Progress.h);
}

TEST(QmLegacyMediaHudContent, ActiveLyricsReserveTheRowDuringAnInterludeWithoutMetadata)
{
	const auto CurrentContent = QmLegacyMediaHudContent(false, false, true, true);
	const auto InterludeContent = QmLegacyMediaHudContent(false, false, false, true);
	ASSERT_TRUE(InterludeContent.m_Visible);
	EXPECT_FALSE(InterludeContent.m_ShowMetadata);
	EXPECT_TRUE(InterludeContent.m_ShowLyrics);
	const auto Current = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, CurrentContent);
	const auto Interlude = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, InterludeContent);
	ExpectContained(Interlude.m_Panel, Interlude.m_Lyrics);
	EXPECT_FLOAT_EQ(Interlude.m_Panel.h, Current.m_Panel.h);
	EXPECT_FLOAT_EQ(Interlude.m_Lyrics.y, Current.m_Lyrics.y);
	EXPECT_FLOAT_EQ(Interlude.m_Cover.w, 0.0f);
}

TEST(QmLegacyMediaHudContent, DisablingLyricsRestoresTheCompactMetadataPanel)
{
	const auto Expanded = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, QmLegacyMediaHudContent(true, true, true, true));
	const auto CompactContent = QmLegacyMediaHudContent(true, true, false, false);
	ASSERT_TRUE(CompactContent.m_Visible);
	EXPECT_FALSE(CompactContent.m_ShowLyrics);
	const auto Compact = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, CompactContent);
	EXPECT_LT(Compact.m_Panel.h, Expanded.m_Panel.h);
	EXPECT_FLOAT_EQ(Compact.m_Lyrics.h, 0.0f);
	EXPECT_FLOAT_EQ(Compact.m_Title.y, Expanded.m_Title.y);
	EXPECT_FLOAT_EQ(Compact.m_Artist.y, Expanded.m_Artist.y);
}

TEST(QmLegacyMediaHudLayout, CoverMetadataProgressAndLyricsHaveSeparateRegions)
{
	const auto Layout = QmLegacyMediaHudLayout(25.0f, 50.0f, 400.0f, 300.0f, QmLegacyMediaHudContent(true, true, true, true));
	const std::array<CUIRect, 5> aRegions = {Layout.m_Cover, Layout.m_Title, Layout.m_Artist, Layout.m_Progress, Layout.m_Lyrics};
	for(size_t First = 0; First < aRegions.size(); ++First)
	{
		SCOPED_TRACE(First);
		ExpectContained(Layout.m_Panel, aRegions[First]);
		for(size_t Second = First + 1; Second < aRegions.size(); ++Second)
		{
			SCOPED_TRACE(Second);
			ExpectSeparate(aRegions[First], aRegions[Second]);
		}
	}
}

TEST(QmLegacyMediaHudLayout, AddingLyricsKeepsTheCoverAtItsMetadataAnchor)
{
	const auto Compact = QmLegacyMediaHudLayout(25.0f, 60.0f, 400.0f, 300.0f, QmLegacyMediaHudContent(true, true, false, false));
	const auto Expanded = QmLegacyMediaHudLayout(25.0f, 60.0f, 400.0f, 300.0f, QmLegacyMediaHudContent(true, true, true, true));
	EXPECT_FLOAT_EQ(Expanded.m_Cover.x, Compact.m_Cover.x);
	EXPECT_FLOAT_EQ(Expanded.m_Cover.y, Compact.m_Cover.y);
	EXPECT_FLOAT_EQ(Expanded.m_Cover.w, Compact.m_Cover.w);
	EXPECT_FLOAT_EQ(Expanded.m_Cover.h, Compact.m_Cover.h);
	EXPECT_GT(Expanded.m_Panel.h, Compact.m_Panel.h);
}

TEST(QmLegacyMediaHudLayout, OffscreenAnchorsAndSmallViewportsKeepEveryRegionInsideTheScreen)
{
	struct SCase
	{
		float m_AnchorX;
		float m_CenterY;
		float m_Width;
		float m_Height;
	};
	const SCase aCases[] = {
		{-200.0f, -100.0f, 400.0f, 300.0f},
		{500.0f, 350.0f, 400.0f, 300.0f},
		{30.0f, 20.0f, 60.0f, 300.0f},
		{300.0f, 50.0f, 400.0f, 20.0f},
		{10.0f, 10.0f, 1.0f, 1.0f},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(::testing::Message() << Case.m_Width << " x " << Case.m_Height << " at " << Case.m_AnchorX << ", " << Case.m_CenterY);
		const auto Layout = QmLegacyMediaHudLayout(Case.m_AnchorX, Case.m_CenterY, Case.m_Width, Case.m_Height, QmLegacyMediaHudContent(true, true, true, true));
		ExpectContained({0.0f, 0.0f, Case.m_Width, Case.m_Height}, Layout.m_Panel);
		for(const auto &Region : {Layout.m_Cover, Layout.m_Title, Layout.m_Artist, Layout.m_Progress, Layout.m_Lyrics})
			ExpectContained(Layout.m_Panel, Region);
		EXPECT_FLOAT_EQ(Layout.m_Cover.w, Layout.m_Cover.h);
	}
}

TEST(QmLegacyMediaHudLyrics, RepeatedSnapshotKeepsTheOriginalScrollStart)
{
	CQmLegacyMediaHudLyricState State;
	const auto Track = Media();
	State.Update(Track, "Same lyric", 1000);
	State.Update(Track, "Same lyric", 2500);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 2.0f);
}

TEST(QmLegacyMediaHudLyrics, NewLyricStartsItsOwnScrollPause)
{
	CQmLegacyMediaHudLyricState State;
	const auto Track = Media();
	State.Update(Track, "First lyric", 1000);
	ASSERT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 2.0f);
	State.Update(Track, "Second lyric", 3000);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 0.0f);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(3500, 1000), 0.5f);
}

TEST(QmLegacyMediaHudLyrics, ChangingTrackIdentityResetsScrollingEvenWhenTheLyricIsUnchanged)
{
	const auto FirstTrack = Media();
	const std::array<CSystemMediaControls::SState, 3> aNextTracks = {
		Media("music-player", "Other song", "Artist"),
		Media("music-player", "Song", "Other artist"),
		Media("other-player", "Song", "Artist"),
	};
	for(const auto &NextTrack : aNextTracks)
	{
		SCOPED_TRACE(::testing::Message() << NextTrack.m_aSourceAppId << ": " << NextTrack.m_aTitle << " / " << NextTrack.m_aArtist);
		CQmLegacyMediaHudLyricState State;
		State.Update(FirstTrack, "Same lyric", 1000);
		State.Update(NextTrack, "Same lyric", 3000);
		EXPECT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 0.0f);
		EXPECT_FLOAT_EQ(State.ElapsedSeconds(4000, 1000), 1.0f);
	}
}

TEST(QmLegacyMediaHudLyrics, ResetAllowsTheSameTrackAndLyricToStartAgain)
{
	CQmLegacyMediaHudLyricState State;
	const auto Track = Media();
	State.Update(Track, "Same lyric", 1000);
	ASSERT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 2.0f);
	State.Reset();
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 0.0f);
	State.Update(Track, "Same lyric", 4000);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(4000, 1000), 0.0f);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(4500, 1000), 0.5f);
}

TEST(QmLegacyMediaHudLyrics, ClockRollbackRestartsThePauseAfterTimeHasAdvanced)
{
	CQmLegacyMediaHudLyricState State;
	const auto Track = Media();
	State.Update(Track, "Same lyric", 1000);
	State.Update(Track, "Same lyric", 3000);
	ASSERT_FLOAT_EQ(State.ElapsedSeconds(3000, 1000), 2.0f);
	State.Update(Track, "Same lyric", 2000);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(2000, 1000), 0.0f);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(2500, 1000), 0.5f);
}

TEST(QmLegacyMediaHudLyrics, InvalidClockFrequencyDoesNotLoseTheScrollStart)
{
	CQmLegacyMediaHudLyricState State;
	State.Update(Media(), "Lyric", 1000);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(2000, 0), 0.0f);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(2000, -1000), 0.0f);
	EXPECT_FLOAT_EQ(State.ElapsedSeconds(2000, 1000), 1.0f);
}
