#include <game/client/components/qmclient/water_hammer_indicator_logic.h>

#include <gtest/gtest.h>

TEST(QmWaterHammerIndicator, PenaltyAreaAndHammerFireMarksPlayer)
{
	EXPECT_TRUE(QmShouldMarkWaterHammer(true, true, true));
}

TEST(QmWaterHammerIndicator, OutsidePenaltyAreaDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(false, true, true));
}

TEST(QmWaterHammerIndicator, NonHammerInputDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(true, false, true));
}

TEST(QmWaterHammerIndicator, ReleasedFireDoesNotMarkPlayer)
{
	EXPECT_FALSE(QmShouldMarkWaterHammer(true, true, false));
}

TEST(QmWaterHammerIndicator, AllDeathAndFreezeTilesArePenaltyTiles)
{
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_DEATH));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_FREEZE));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_DFREEZE));
	EXPECT_TRUE(QmIsWaterHammerPenaltyTile(TILE_LFREEZE));
	EXPECT_FALSE(QmIsWaterHammerPenaltyTile(TILE_AIR));
}
