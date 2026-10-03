#include <game/client/components/qmclient/water_hammer_indicator_logic.h>

#include <gtest/gtest.h>

TEST(QmWaterHammerIndicator, TimedFreezeAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(200, false, true, true));
}

TEST(QmWaterHammerIndicator, UnfrozenHammerFireDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(0, false, true, true));
}

TEST(QmWaterHammerIndicator, DeepFreezeAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(-1, false, true, true));
}

TEST(QmWaterHammerIndicator, LiveFreezeAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(0, true, true, true));
}

TEST(QmWaterHammerIndicator, NonHammerInputDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, false, true));
}

TEST(QmWaterHammerIndicator, ReleasedFireDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(200, false, true, false));
}

TEST(QmWaterHammerIndicator, AllDeathAndFreezeTilesArePenaltyTiles)
{
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_DEATH));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_FREEZE));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_DFREEZE));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_LFREEZE));
	EXPECT_FALSE(QmIsWaterHammerPenaltyTile(TILE_AIR));
}
