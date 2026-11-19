#include <game/client/components/qmclient/console_scroll.h>

#include <gtest/gtest.h>

#include <cmath>

TEST(QmConsoleWheel, DefaultStepUsesThreeLines)
{
	EXPECT_EQ(QmConsoleWheelLines(1.0f, 20), 3);
}

TEST(QmConsoleWheel, GlobalStepScalesTheSemanticBaseLineCount)
{
	EXPECT_EQ(QmConsoleWheelLines(2.0f, 20), 6);
	EXPECT_EQ(QmConsoleWheelLines(10.0f, 20), 8);
}

TEST(QmConsoleWheel, NarrowViewportLowersTheCap)
{
	// 六行目标在四行日志区最多推进三行，避免一档滚出视口。
	EXPECT_EQ(QmConsoleWheelLines(2.0f, 4), 3);
	EXPECT_EQ(QmConsoleWheelLines(1.0f, 3), 2);
	EXPECT_EQ(QmConsoleWheelLines(1.0f, 1), 1);
}

TEST(QmConsoleWheel, UnknownGeometryAndInvalidScaleUseSafeFallbacks)
{
	// 几何未知时只使用语义基准，不因视口为 0 退化成 1 行。
	EXPECT_EQ(QmConsoleWheelLines(1.0f, 0), 3);
	EXPECT_EQ(QmConsoleWheelLines(1.0f, -1), 3);
	EXPECT_EQ(QmConsoleWheelLines(0.0f, 20), 1);
	EXPECT_EQ(QmConsoleWheelLines(std::nanf(""), 20), 3);
}

TEST(QmConsoleLogClip, StartsAtToolbarBottomWithoutWholeLineRounding)
{
	const SQmConsoleLogClipBounds Bounds = QmConsoleResolveLogClipBounds(40.0f, 400.0f, 2.0f);
	// 日志顶部直接取工具栏下沿；旧实现会按整行向下取整，把顶部不足一行裁掉。
	EXPECT_FLOAT_EQ(Bounds.m_Top, 80.0f);
	EXPECT_FLOAT_EQ(Bounds.m_Bottom, 804.0f);
	EXPECT_FLOAT_EQ(Bounds.Height(), 724.0f);
}

TEST(QmConsoleLogClip, DegenerateBoundsNeverProduceNegativeSize)
{
	const SQmConsoleLogClipBounds Bounds = QmConsoleResolveLogClipBounds(-10.0f, -20.0f, 0.0f);
	EXPECT_FLOAT_EQ(Bounds.m_Top, 0.0f);
	EXPECT_FLOAT_EQ(Bounds.m_Bottom, 0.0f);
	EXPECT_FLOAT_EQ(Bounds.Height(), 0.0f);
}

TEST(QmConsoleLogClip, SubLineOffsetMovesTheFirstVisibleLineDown)
{
	// 已经隐藏 6 行、亚行余量 3px：首行内容应再向下补 3px。
	EXPECT_FLOAT_EQ(QmConsoleLineStackOffset(6, 6, 10.0f, 3.0f), 3.0f);
	EXPECT_FLOAT_EQ(QmConsoleLineStackOffset(8, 2, 10.0f, 0.0f), 60.0f);
}

TEST(QmConsoleLogClip, SubLineOffsetKeepsScrollDirectionContinuous)
{
	// 半行阶段继续沿同一方向推进：偏移越大，内容越往下（展示更早的日志）。
	const float Before = QmConsoleLineStackOffset(6, 0, 20.0f, 7.0f);
	const float After = QmConsoleLineStackOffset(6, 0, 20.0f, 11.0f);
	EXPECT_FLOAT_EQ(Before, 127.0f);
	EXPECT_FLOAT_EQ(After, 131.0f);
	EXPECT_GT(After, Before);

	// 跨过整行边界后仍连续：6 行 + 19px 与 7 行 + 0px 只差 1px。
	EXPECT_FLOAT_EQ(QmConsoleLineStackOffset(7, 0, 20.0f, 0.0f), 140.0f);
	EXPECT_FLOAT_EQ(QmConsoleLineStackOffset(6, 0, 20.0f, 19.0f), 139.0f);
}

TEST(QmConsoleLogClip, PartiallyVisibleTopLineStillHasVisiblePixels)
{
	constexpr float ClipTop = 100.0f;
	EXPECT_TRUE(QmScrollLineVisibleInViewportTop(90.0f, 110.0f, ClipTop));
	EXPECT_TRUE(QmScrollLineVisibleInViewportTop(100.0f, 120.0f, ClipTop));
	EXPECT_FALSE(QmScrollLineVisibleInViewportTop(80.0f, 100.0f, ClipTop));
	EXPECT_FALSE(QmScrollLineVisibleInViewportTop(80.0f, 80.0f, ClipTop));
}
