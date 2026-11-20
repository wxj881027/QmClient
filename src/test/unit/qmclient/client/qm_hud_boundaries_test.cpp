#include <game/client/components/hud.h>

#include <gtest/gtest.h>

#include <limits>
#include <memory>

TEST(QmHudMiniDebug, MissingLocalClientHidesDebugInfo)
{
	EXPECT_FALSE(QmHudMiniDebugTarget(false, -1, 0).has_value());
}

TEST(QmHudMiniDebug, PlayingUsesLocalClient)
{
	EXPECT_EQ(QmHudMiniDebugTarget(false, 0, SPEC_FREEVIEW), 0);
}

TEST(QmHudMiniDebug, FollowingUsesSpectatedClient)
{
	EXPECT_EQ(QmHudMiniDebugTarget(true, 0, MAX_CLIENTS - 1), MAX_CLIENTS - 1);
}

TEST(QmHudMiniDebug, FreeViewShowsCoordinatesWithoutAClient)
{
	EXPECT_EQ(QmHudMiniDebugTarget(true, -1, SPEC_FREEVIEW), SPEC_FREEVIEW);
}

TEST(QmHudMiniDebug, InvalidFollowTargetHidesDebugInfo)
{
	EXPECT_FALSE(QmHudMiniDebugTarget(true, 0, MAX_CLIENTS).has_value());
}

TEST(QmHudBoundaries, PersonalRecordRequiresAnIndexForTheLocalClient)
{
	EXPECT_FALSE(QmHudHasClientTarget(-1));
	EXPECT_FALSE(QmHudHasClientTarget(MAX_CLIENTS));
	EXPECT_TRUE(QmHudHasClientTarget(0));
}

TEST(QmHudBoundaries, SnapshotSpeedKeepsThePreviousSampleUntilTheNextTick)
{
	EXPECT_EQ(QmHudSnapshotSpeed(-4, 9, 0.75f), 4u);
	EXPECT_EQ(QmHudSnapshotSpeed(-4, 9, 1.0f), 9u);
}

TEST(QmHudBoundaries, SnapshotSpeedCanRepresentTheMagnitudeOfAnyVelocity)
{
	const int Lowest = std::numeric_limits<int>::min();
	const int Highest = std::numeric_limits<int>::max();
	EXPECT_EQ(QmHudSnapshotSpeed(Lowest, Highest, 0.0f), static_cast<uint32_t>(static_cast<int64_t>(Highest) + 1));
	EXPECT_EQ(QmHudSnapshotSpeed(Lowest, Highest, 1.0f), static_cast<uint32_t>(Highest));
}

TEST(QmHudBoundaries, NinjaProgressClipsExpiredAndFullBars)
{
	EXPECT_FLOAT_EQ(QmHudNinjaProgress(10, 20, 20), 0.0f);
	EXPECT_FLOAT_EQ(QmHudNinjaProgress(30, 20, 20), 0.5f);
	EXPECT_FLOAT_EQ(QmHudNinjaProgress(50, 20, 20), 1.0f);
	EXPECT_FLOAT_EQ(QmHudNinjaProgress(50, 20, 0), 0.0f);
}

TEST(QmHudBoundaries, NinjaProgressIncludesAnExpirationPastTheLastIntTick)
{
	const int Current = std::numeric_limits<int>::max();
	EXPECT_FLOAT_EQ(QmHudNinjaProgress(static_cast<int64_t>(Current) + 10, Current, 20), 0.5f);
}

TEST(QmHudBoundaries, RaceEffectHoldsForFourSecondsAndFadesForTwo)
{
	EXPECT_FLOAT_EQ(QmHudRaceEffectAlpha(250, 50, 50), 1.0f);
	EXPECT_FLOAT_EQ(QmHudRaceEffectAlpha(300, 50, 50), 0.5f);
	EXPECT_FLOAT_EQ(QmHudRaceEffectAlpha(350, 50, 50), 0.0f);
}

TEST(QmHudBoundaries, RaceEffectStillFadesCloseToTheLastIntTick)
{
	const int Current = std::numeric_limits<int>::max();
	EXPECT_FLOAT_EQ(QmHudRaceEffectAlpha(Current, Current - 250, 50), 0.5f);
}

TEST(QmHudBoundaries, ScoreHudRecreatesUnchangedTextAfterAtlasInvalidation)
{
	auto pRevision = std::make_shared<uint64_t>(1);
	STextContainerIndex Index;
	Index.m_Index = 0;
	Index.m_UseCount->BindGlyphAtlas(pRevision);

	EXPECT_FALSE(SScoreInfo::NeedsTextContainer(Index, "42"));
	++*pRevision;
	EXPECT_TRUE(SScoreInfo::NeedsTextContainer(Index, "42"));
}

TEST(QmHudBoundaries, ScoreHudSkipsRecreationForEmptyText)
{
	const STextContainerIndex Index;
	EXPECT_FALSE(SScoreInfo::NeedsTextContainer(Index, ""));
}
