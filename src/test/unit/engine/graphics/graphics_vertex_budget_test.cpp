#include <engine/client/graphics_vertex_budget.h>

#include <gtest/gtest.h>

#include <limits>

TEST(GraphicsTextRetention, ShortTextFitsWithSpaceForTheNextQuad)
{
	EXPECT_TRUE(GraphicsCanRetainTextQuads(0));
	EXPECT_TRUE(GraphicsCanRetainTextQuads(100));
	EXPECT_TRUE(GraphicsCanRetainTextQuads(5460));
}

TEST(GraphicsTextRetention, TextAtTheAutomaticFlushBoundaryRequiresIndependentPasses)
{
	EXPECT_FALSE(GraphicsCanRetainTextQuads(5461));
	EXPECT_FALSE(GraphicsCanRetainTextQuads(8191));
	EXPECT_FALSE(GraphicsCanRetainTextQuads(std::numeric_limits<size_t>::max()));
}

TEST(GraphicsRecolorRange, OnlyCompleteRetainedQuadsCanBeRecolored)
{
	EXPECT_TRUE(GraphicsCanRecolorQuad(0, 8, 4));
	EXPECT_TRUE(GraphicsCanRecolorQuad(1, 8, 4));
	EXPECT_FALSE(GraphicsCanRecolorQuad(2, 8, 4));
	EXPECT_TRUE(GraphicsCanRecolorQuad(1, 12, 6));
	EXPECT_FALSE(GraphicsCanRecolorQuad(2, 12, 6));
	EXPECT_FALSE(GraphicsCanRecolorQuad(1, 7, 4));
}

TEST(GraphicsRecolorRange, InvalidCountsAndOverflowingOffsetsAreRejectedBeforeMultiplication)
{
	EXPECT_FALSE(GraphicsCanRecolorQuad(0, 0, 4));
	EXPECT_FALSE(GraphicsCanRecolorQuad(0, -1, 4));
	EXPECT_FALSE(GraphicsCanRecolorQuad(0, std::numeric_limits<int>::max(), 4));
	EXPECT_FALSE(GraphicsCanRecolorQuad(0, 8, 0));
	EXPECT_FALSE(GraphicsCanRecolorQuad(std::numeric_limits<size_t>::max(), 8, 4));
}
