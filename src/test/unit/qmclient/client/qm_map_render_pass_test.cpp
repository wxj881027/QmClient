#include <game/map/render_interfaces.h>

#include <gtest/gtest.h>

TEST(QmMapRenderPass, BackgroundStopsBeforeGameLayerWithinSharedGroup)
{
	for(const int Type : {RENDERTYPE_BACKGROUND, RENDERTYPE_BACKGROUND_FORCE})
	{
		EXPECT_TRUE(MapRenderLayerInPass(Type, 1, 1, 1, 2));
		EXPECT_FALSE(MapRenderLayerInPass(Type, 1, 2, 1, 2));
		EXPECT_FALSE(MapRenderLayerInPass(Type, 1, 3, 1, 2));
		EXPECT_TRUE(MapRenderLayerInPass(Type, 0, 9, 1, 2));
		EXPECT_FALSE(MapRenderLayerInPass(Type, 2, 0, 1, 2));
	}
}

TEST(QmMapRenderPass, ForegroundIncludesGameAndFollowingLayers)
{
	EXPECT_FALSE(MapRenderLayerInPass(RENDERTYPE_FOREGROUND, 0, 9, 1, 2));
	EXPECT_FALSE(MapRenderLayerInPass(RENDERTYPE_FOREGROUND, 1, 1, 1, 2));
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FOREGROUND, 1, 2, 1, 2));
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FOREGROUND, 1, 3, 1, 2));
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FOREGROUND, 2, 0, 1, 2));
}

TEST(QmMapRenderPass, GroupProjectionAndClipUpdatesRemainEnabledInEveryPass)
{
	for(const int Type : {RENDERTYPE_BACKGROUND, RENDERTYPE_BACKGROUND_FORCE, RENDERTYPE_FOREGROUND, RENDERTYPE_FULL_DESIGN})
	{
		EXPECT_TRUE(MapRenderLayerInPass(Type, 0, -1, 1, 2));
		EXPECT_TRUE(MapRenderLayerInPass(Type, 1, -1, 1, 2));
		EXPECT_TRUE(MapRenderLayerInPass(Type, 2, -1, 1, 2));
	}
}

TEST(QmMapRenderPass, MissingGameLayerLeavesEntireDesignInBackground)
{
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_BACKGROUND_FORCE, 1, 3, -1, -1));
	EXPECT_FALSE(MapRenderLayerInPass(RENDERTYPE_FOREGROUND, 1, 3, -1, -1));
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FULL_DESIGN, 1, 3, -1, -1));
}

TEST(QmMapRenderPass, FullDesignIncludesBothSidesOfGameBoundary)
{
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FULL_DESIGN, 1, 1, 1, 2));
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FULL_DESIGN, 1, 2, 1, 2));
	EXPECT_TRUE(MapRenderLayerInPass(RENDERTYPE_FULL_DESIGN, 2, 0, 1, 2));
}
