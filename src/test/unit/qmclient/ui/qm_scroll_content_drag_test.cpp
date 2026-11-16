#include <game/client/ui_scrollregion.h>

#include <gtest/gtest.h>

// 内容区拖动滚动：起手抖动必须被阈值挡住，超过阈值才升级为拖动。
TEST(QmScrollContentDrag, SmallMovementStaysCandidate)
{
	const float Threshold = QmScrollRegionContentDragThreshold(1.0f);
	EXPECT_FALSE(QmScrollRegionContentDragBecomesActive(false, 0.0f, Threshold));
	EXPECT_FALSE(QmScrollRegionContentDragBecomesActive(false, Threshold * 0.5f, Threshold));
	EXPECT_FALSE(QmScrollRegionContentDragBecomesActive(false, -Threshold * 0.5f, Threshold));
	EXPECT_FALSE(QmScrollRegionContentDragBecomesActive(false, Threshold - 0.5f, Threshold));
}

TEST(QmScrollContentDrag, LargerMovementActivatesBothDirections)
{
	const float Threshold = QmScrollRegionContentDragThreshold(1.0f);
	EXPECT_TRUE(QmScrollRegionContentDragBecomesActive(false, Threshold, Threshold));
	EXPECT_TRUE(QmScrollRegionContentDragBecomesActive(false, Threshold + 1.0f, Threshold));
	EXPECT_TRUE(QmScrollRegionContentDragBecomesActive(false, -Threshold, Threshold));
	EXPECT_TRUE(QmScrollRegionContentDragBecomesActive(false, -Threshold - 1.0f, Threshold));
}

// 一旦进入拖动，后续帧即使手指回到起点也保持拖动，滚动不会中途卡住。
TEST(QmScrollContentDrag, ActiveDragStaysActive)
{
	const float Threshold = QmScrollRegionContentDragThreshold(1.0f);
	EXPECT_TRUE(QmScrollRegionContentDragBecomesActive(true, 0.0f, Threshold));
	EXPECT_TRUE(QmScrollRegionContentDragBecomesActive(true, Threshold * 0.25f, Threshold));
}

// 阈值按物理像素缩放：高 DPI 屏幕上同样的手指位移不应被误判成点击。
TEST(QmScrollContentDrag, ThresholdScalesWithPixelSize)
{
	const float UnitThreshold = QmScrollRegionContentDragThreshold(1.0f);
	const float DenseThreshold = QmScrollRegionContentDragThreshold(2.0f);
	EXPECT_FLOAT_EQ(DenseThreshold, UnitThreshold * 2.0f);
	EXPECT_GT(UnitThreshold, 0.0f);
	// 无像素信息时退回 1 个像素，保证阈值不会退化成 0。
	EXPECT_FLOAT_EQ(QmScrollRegionContentDragThreshold(0.0f), QmScrollRegionContentDragThreshold(1.0f));
}
// 甩动必须真的产生惯性位移，并在摩擦下停下来，而不是松手即停。
TEST(QmScrollContentDrag, FlingMovesContentThenSettles)
{
	CQmScrollState State;
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 1000.0f;
	SQmScrollConfig Config;
	Config.m_NativeWheelStep = false;
	Config.m_MaxOverscroll = 0.0f;

	State.Fling(800.0f, Metrics);
	EXPECT_GT(State.Velocity(), 0.0f);

	State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_GT(State.Offset(), 0.0f);

	const float AfterFirstFrame = State.Offset();
	for(int Frame = 0; Frame < 240 && std::abs(State.Velocity()) > 0.0f; ++Frame)
		State.Advance(1.0f / 60.0f, Metrics, Config);

	// 惯性继续推进了一段，但被摩擦收住，不会越过内容末尾。
	EXPECT_GT(State.Offset(), AfterFirstFrame);
	EXPECT_LE(State.Offset(), Metrics.MaxOffset() + 0.5f);
	EXPECT_FLOAT_EQ(State.Velocity(), 0.0f);
}

// 负向甩动反向滚动，不能因为速度符号处理错误变成单向。
TEST(QmScrollContentDrag, FlingRespectsDirection)
{
	CQmScrollState State;
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 1000.0f;
	SQmScrollConfig Config;
	Config.m_NativeWheelStep = false;

	State.SetOffset(500.0f, Metrics, Config);
	State.Fling(-900.0f, Metrics);
	EXPECT_LT(State.Velocity(), 0.0f);
	State.Advance(1.0f / 60.0f, Metrics, Config);
	EXPECT_LT(State.Offset(), 500.0f);
}

// 极端速度要按上限截断，否则一次猛甩会直接跳到列表末尾。
TEST(QmScrollContentDrag, FlingVelocityIsClamped)
{
	CQmScrollState Extreme;
	CQmScrollState Clamped;
	SQmScrollMetrics Metrics;
	Metrics.m_ViewportSize = 100.0f;
	Metrics.m_ContentSize = 100000.0f;
	SQmScrollConfig Config;
	Config.m_NativeWheelStep = false;

	Extreme.Fling(1000000.0f, Metrics);
	Clamped.Fling(QM_SCROLL_MAX_FLING_VELOCITY, Metrics);
	EXPECT_FLOAT_EQ(Extreme.Velocity(), Clamped.Velocity());
}
