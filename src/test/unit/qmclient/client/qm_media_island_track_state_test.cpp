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
	SHudMediaIslandTrackInput Track(const char *pTitle, const char *pArtist = "", const char *pAlbum = "")
	{
		SHudMediaIslandTrackInput Input;
		Input.m_pTitle = pTitle;
		Input.m_pArtist = pArtist;
		Input.m_pAlbum = pAlbum;
		return Input;
	}
	EHudMediaIslandTrackUpdate ApplyTrack(
		SHudMediaIslandTrackSnapshot &Current,
		SHudMediaIslandTrackSnapshot &Outgoing,
		bool &HasIdentity,
		bool &TransitionActive,
		bool &NeedsNodeReset,
		int64_t &StartTick,
		int64_t Now,
		const SHudMediaIslandTrackInput &Input)
	{
		return QmHudMediaIslandUpdateTrackSnapshots(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, Now, Input);
	}
}

TEST(QmHudMediaIslandLogic, FirstMediaStateDoesNotStartTrackTransition)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::FIRST_IDENTITY);
	EXPECT_TRUE(HasIdentity);
	EXPECT_FALSE(TransitionActive);
	EXPECT_FALSE(NeedsNodeReset);
	EXPECT_STREQ(Current.m_aTitle, "Song A");
	EXPECT_FALSE(Outgoing.HasMeaningfulIdentity());
}

TEST(QmHudMediaIslandLogic, FirstOrLateMetadataStartsTheTrackDetailsDeadline)
{
	EXPECT_TRUE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::FIRST_IDENTITY, false, true));
	EXPECT_TRUE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::NONE, false, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::NONE, true, true));
	EXPECT_FALSE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::FIRST_IDENTITY, false, false));
	EXPECT_TRUE(QmHudMediaIslandShouldRevealTrackDetails(EHudMediaIslandTrackUpdate::TRACK_CHANGED, true, true));
}

TEST(QmHudMediaIslandLogic, TrackChangeCopiesCurrentSnapshotToOutgoing)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A", "Album A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song B", "Artist B", "Album B"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::TRACK_CHANGED);
	EXPECT_TRUE(TransitionActive);
	EXPECT_TRUE(NeedsNodeReset);
	EXPECT_EQ(StartTick, 200);
	EXPECT_STREQ(Outgoing.m_aTitle, "Song A");
	EXPECT_STREQ(Current.m_aTitle, "Song B");
}

TEST(QmHudMediaIslandLogic, ContinuousTrackChangesKeepOnlyLatestTrack)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));
	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song B", "Artist B"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 250, Track("Song C", "Artist C"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::TRACK_CHANGED);
	EXPECT_STREQ(Outgoing.m_aTitle, "Song B");
	EXPECT_STREQ(Current.m_aTitle, "Song C");
	EXPECT_TRUE(TransitionActive);
}

TEST(QmHudMediaIslandLogic, TitleChangeWithEmptyArtistStillCountsAsTrackChange)
{
	SHudMediaIslandTrackSnapshot Current;
	Current.SetFrom(Track("Song A"));

	EXPECT_TRUE(QmHudMediaIslandTrackChanged(Current, Track("Song B")));
}

TEST(QmHudMediaIslandLogic, SameTrackRefreshDoesNotRestartTransition)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song A", "Artist A"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::NONE);
	EXPECT_FALSE(TransitionActive);
	EXPECT_FALSE(NeedsNodeReset);
	EXPECT_FALSE(Outgoing.HasMeaningfulIdentity());
}

TEST(QmHudMediaIslandLogic, EmptyMetadataRefreshKeepsLastStableSnapshot)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A", "Artist A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track(""));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::NONE);
	EXPECT_STREQ(Current.m_aTitle, "Song A");
	EXPECT_STREQ(Current.m_aArtist, "Artist A");
	EXPECT_FALSE(TransitionActive);
}

TEST(QmHudMediaIslandLogic, MissingCoverSnapshotsStillSwitch)
{
	SHudMediaIslandTrackSnapshot Current;
	SHudMediaIslandTrackSnapshot Outgoing;
	bool HasIdentity = false;
	bool TransitionActive = false;
	bool NeedsNodeReset = false;
	int64_t StartTick = 0;

	ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 100, Track("Song A"));
	const EHudMediaIslandTrackUpdate Update = ApplyTrack(Current, Outgoing, HasIdentity, TransitionActive, NeedsNodeReset, StartTick, 200, Track("Song B"));

	EXPECT_EQ(Update, EHudMediaIslandTrackUpdate::TRACK_CHANGED);
	EXPECT_FALSE(Current.m_HasCover);
	EXPECT_FALSE(Outgoing.m_HasCover);
}

TEST(QmHudMediaIslandLogic, Utf8TitlesStayNulTerminatedInFixedSnapshot)
{
	SHudMediaIslandTrackSnapshot Current;
	const char *pTitle = "很长的中文标题 Mixed UTF-8 Title 很长的中文标题 Mixed UTF-8 Title 很长的中文标题 Mixed UTF-8 Title 很长的中文标题 Mixed UTF-8 Title";

	Current.SetFrom(Track(pTitle, "艺术家"));

	EXPECT_NE(Current.m_aTitle[0], '\0');
	EXPECT_EQ(Current.m_aTitle[sizeof(Current.m_aTitle) - 1], '\0');
	EXPECT_STREQ(Current.m_aArtist, "艺术家");
}

TEST(QmHudMediaIslandLogic, TopEffectMovesBelowOverlappingIsland)
{
	const CUIRect Island = {120.0f, 1.0f, 80.0f, 38.0f};

	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 10.0f, 140.0f, 180.0f, Island, true), 42.0f);
}

TEST(QmHudMediaIslandLogic, TopEffectDoesNotMoveForHorizontalSeparationOrHiddenIsland)
{
	const CUIRect SideIsland = {20.0f, 1.0f, 60.0f, 38.0f};
	const CUIRect CenterIsland = {120.0f, 1.0f, 80.0f, 38.0f};

	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 10.0f, 140.0f, 180.0f, SideIsland, true), 20.0f);
	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 10.0f, 140.0f, 180.0f, CenterIsland, false), 20.0f);
}

TEST(QmHudMediaIslandLogic, TopEffectDoesNotMoveForIslandBelowIt)
{
	const CUIRect LowerIsland = {120.0f, 100.0f, 80.0f, 38.0f};

	EXPECT_FLOAT_EQ(QmHudTopEffectY(20.0f, 24.0f, 140.0f, 180.0f, LowerIsland, true), 20.0f);
}

TEST(QmHudMediaIslandLogic, TeamZeroDoesNotCreateTeamDisplay)
{
	EXPECT_FALSE(QmHudMediaIslandShouldShowTeam(true, true, 0));
	EXPECT_TRUE(QmHudMediaIslandShouldShowTeam(true, true, 1));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTeam(false, true, 1));
	EXPECT_FALSE(QmHudMediaIslandShouldShowTeam(true, false, 1));
}
