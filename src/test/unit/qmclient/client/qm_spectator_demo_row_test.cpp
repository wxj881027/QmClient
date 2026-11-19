#include <game/client/components/spectator.h>

#include <gtest/gtest.h>

TEST(QmSpectatorDemoRow, RankMemberDoesNotReadTheDemoPlayerSnapshot)
{
	bool SnapshotRead = false;
	EXPECT_FALSE(QmSpectatorSelection::ShouldDimDemoPlayer(true, true, [&] {
		SnapshotRead = true;
		return false;
	}));
	EXPECT_FALSE(SnapshotRead);
}

TEST(QmSpectatorDemoRow, OrdinaryDemoStillDimsInactivePlayers)
{
	EXPECT_TRUE(QmSpectatorSelection::ShouldDimDemoPlayer(true, false, [] { return false; }));
	EXPECT_FALSE(QmSpectatorSelection::ShouldDimDemoPlayer(true, false, [] { return true; }));
}
