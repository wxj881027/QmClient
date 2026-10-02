#include <game/client/components/qmclient/qm_skin_outline.h>

#include <gtest/gtest.h>

TEST(QmSkinOutline, SelectsLocalAndOtherPlayers)
{
	EXPECT_TRUE(QmShouldDrawSkinOutline(0, 0, 1, true, false));
	EXPECT_TRUE(QmShouldDrawSkinOutline(2, 0, 1, false, true));
	EXPECT_FALSE(QmShouldDrawSkinOutline(2, 0, 1, true, false));
	EXPECT_FALSE(QmShouldDrawSkinOutline(-1, 0, 1, true, true));
}
