#include <game/client/components/qmclient/console_scroll.h>

#include <gtest/gtest.h>

TEST(QmConsoleWheel, SmallStepStillScrollsAUsefulNumberOfLines)
{
	// 默认 10px 步长配 14px 行高：折算不到 1 行，取下限 3 行。
	EXPECT_EQ(QmConsoleWheelLines(10.0f, 14.0f), 3);
	EXPECT_EQ(QmConsoleWheelLines(2.0f, 14.0f), 3);
}

TEST(QmConsoleWheel, StepIsConvertedByLineHeightNotPixels)
{
	// 同样 40px 的步长：行高 14 折算约 3 行，行高 26 折算约 1.5 行（保底 3 行）。
	EXPECT_EQ(QmConsoleWheelLines(40.0f, 14.0f), 3);
	EXPECT_EQ(QmConsoleWheelLines(40.0f, 26.0f), 3);
	// 130px / 26px = 5 行：上下限之间如实折算。
	EXPECT_EQ(QmConsoleWheelLines(130.0f, 26.0f), 5);
}

TEST(QmConsoleWheel, ClampsToEightLinesAndFallsBackOnInvalidInput)
{
	// 超大步长最多 8 行，避免一档翻过整屏。
	EXPECT_EQ(QmConsoleWheelLines(1000.0f, 10.0f), 8);
	// 未跑配置加载（步长 0）或行高未初始化时用安全值，不产生除零。
	EXPECT_EQ(QmConsoleWheelLines(0.0f, 0.0f), 8);
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
