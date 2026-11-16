#include <game/client/components/qmclient/console_scroll.h>
#include <game/client/QmUi/QmScroll.h>

#include <gtest/gtest.h>

TEST(QmConsoleWheel, SmallStepStillScrollsAUsefulNumberOfLines)
{
	// 默认 10px 步长配 14px 行高：折算不到 1 行，取下限 1 行。
	EXPECT_EQ(QmConsoleWheelLines(10.0f, 14.0f), 1);
	EXPECT_EQ(QmConsoleWheelLines(2.0f, 14.0f), 1);
}

TEST(QmConsoleWheel, StepIsConvertedByLineHeightNotPixels)
{
	// 同样 40px 的步长：行高 14 折算约 3 行，行高 26 折算约 1.5 行（保留 2 行）。
	EXPECT_EQ(QmConsoleWheelLines(40.0f, 14.0f), 3);
	EXPECT_EQ(QmConsoleWheelLines(40.0f, 26.0f), 2);
	// 130px / 26px = 5 行：超过上限，夹到 3 行。
	EXPECT_EQ(QmConsoleWheelLines(130.0f, 26.0f), 3);
}

TEST(QmConsoleWheel, ClampsToThreeLinesAndFallsBackOnInvalidInput)
{
	// 超大步长最多 3 行，避免一档翻过整屏。
	EXPECT_EQ(QmConsoleWheelLines(1000.0f, 10.0f), 3);
	// 未跑配置加载（步长 0）或行高未初始化时用安全值，不产生除零。
	EXPECT_EQ(QmConsoleWheelLines(0.0f, 0.0f), 3);
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
