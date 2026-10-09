#include <game/client/components/qmclient/dummy_miniview_render.h>

#include <gtest/gtest.h>

TEST(QmDummyMiniViewTargetSize, FirstViewAllocatesEnoughCapacity)
{
	const auto Size = ResolveQmDummyMiniViewTargetSize(420, 235, 0, 0, 1920, 1080);
	EXPECT_EQ(Size.m_W, 448);
	EXPECT_EQ(Size.m_H, 256);
}

TEST(QmDummyMiniViewTargetSize, ResizeWithinCapacityReusesTarget)
{
	const auto Size = ResolveQmDummyMiniViewTargetSize(440, 250, 448, 256, 1920, 1080);
	EXPECT_EQ(Size.m_W, 448);
	EXPECT_EQ(Size.m_H, 256);
}

TEST(QmDummyMiniViewTargetSize, ShrinkingKeepsExistingCapacity)
{
	const auto Size = ResolveQmDummyMiniViewTargetSize(210, 118, 448, 256, 1920, 1080);
	EXPECT_EQ(Size.m_W, 448);
	EXPECT_EQ(Size.m_H, 256);
}

TEST(QmDummyMiniViewTargetSize, GrowingOneAxisPreservesOtherAxisCapacity)
{
	const auto Size = ResolveQmDummyMiniViewTargetSize(450, 120, 448, 256, 1920, 1080);
	EXPECT_EQ(Size.m_W, 512);
	EXPECT_EQ(Size.m_H, 256);
}

TEST(QmDummyMiniViewTargetSize, FirstAllocationIsLimitedToScreen)
{
	const auto Size = ResolveQmDummyMiniViewTargetSize(900, 600, 0, 0, 850, 570);
	EXPECT_EQ(Size.m_W, 850);
	EXPECT_EQ(Size.m_H, 570);
}

TEST(QmDummyMiniViewTargetSize, SmallerScreenDoesNotForceTargetDestruction)
{
	const auto Size = ResolveQmDummyMiniViewTargetSize(300, 200, 448, 256, 320, 240);
	EXPECT_EQ(Size.m_W, 448);
	EXPECT_EQ(Size.m_H, 256);
}

TEST(QmDummyMiniViewTargetSize, MinimizedOrEmptyViewDoesNotAllocate)
{
	EXPECT_EQ(ResolveQmDummyMiniViewTargetSize(420, 235, 0, 0, 1920, 0).m_W, 0);
	EXPECT_EQ(ResolveQmDummyMiniViewTargetSize(0, 235, 0, 0, 1920, 1080).m_W, 0);
}
