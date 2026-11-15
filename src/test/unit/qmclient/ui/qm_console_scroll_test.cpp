// 控制台日志滚动的行为测试：滚轮逐帧逼近、越界回弹、拖动跟手、贴底跟随与新日志补偿。
// 断言只走 CQmConsoleScrollRuntime 的生产接口，不引用源码字符串或复制内部积分公式。
#include <game/client/QmUi/QmConsoleScroll.h>

#include <gtest/gtest.h>

namespace
{
	constexpr float LINE_HEIGHT = 10.0f;
	constexpr float VIEWPORT = 120.0f;
	constexpr int TOTAL_LINES = 100;
	constexpr float FRAME_DT = 1.0f / 60.0f;

	// 控制台侧的实际手感档：一档 = 若干行，其余参数仍取公共默认值。
	SQmScrollConfig MakeConfig(int WheelLines = 3, float MaxOverscroll = 0.0f, EQmScrollFeel Feel = EQmScrollFeel::SMOOTH)
	{
		SQmScrollConfig Config;
		Config.m_WheelScale = (float)WheelLines * LINE_HEIGHT;
		Config.m_NativeWheelStep = true;
		Config.m_Feel = Feel;
		Config.m_NativeWheelAnimationTime = 0.2f;
		Config.m_MaxOverscroll = MaxOverscroll;
		Config.m_SpringResponse = 0.3f;
		Config.m_SpringDamping = 0.9f;
		return Config;
	}

	CQmConsoleScrollRuntime::SFrame AdvanceFrames(CQmConsoleScrollRuntime &Runtime, int Frames, int TotalLines = TOTAL_LINES, const SQmScrollConfig &Config = MakeConfig())
	{
		CQmConsoleScrollRuntime::SFrame Frame;
		for(int i = 0; i < Frames; ++i)
			Frame = Runtime.Advance(FRAME_DT, VIEWPORT, TotalLines, LINE_HEIGHT, Config);
		return Frame;
	}
} // namespace

TEST(QmConsoleScroll, WheelApproachesTargetMonotonicallyWithoutOvershoot)
{
	CQmConsoleScrollRuntime Runtime;
	AdvanceFrames(Runtime, 1);
	const SQmScrollConfig Config = MakeConfig();
	Runtime.Wheel(1);

	const float Target = 3.0f * LINE_HEIGHT;
	float Previous = Runtime.Offset();
	ASSERT_FLOAT_EQ(Previous, 0.0f);
	bool Moved = false;
	for(int i = 0; i < 120; ++i)
	{
		const auto Frame = Runtime.Advance(FRAME_DT, VIEWPORT, TOTAL_LINES, LINE_HEIGHT, Config);
		// 逐帧单调逼近：不回头、不越过目标（过冲会让滚轮读成"弹了一下"）。
		EXPECT_GE(Frame.m_Offset, Previous - 0.001f);
		EXPECT_LE(Frame.m_Offset, Target + 0.001f);
		Moved |= Frame.m_Offset > Previous + 0.001f;
		Previous = Frame.m_Offset;
	}
	EXPECT_TRUE(Moved);
	EXPECT_NEAR(Runtime.Offset(), Target, 0.01f);
}

TEST(QmConsoleScroll, AltWheelUsesTheSharedThreeTimesMultiplier)
{
	CQmConsoleScrollRuntime Normal;
	CQmConsoleScrollRuntime Alt;
	const SQmScrollConfig Config = MakeConfig();
	AdvanceFrames(Normal, 1, TOTAL_LINES, Config);
	AdvanceFrames(Alt, 1, TOTAL_LINES, Config);

	Normal.Wheel(1);
	Alt.Wheel(1, true);
	const auto NormalFrame = AdvanceFrames(Normal, 120, TOTAL_LINES, Config);
	const auto AltFrame = AdvanceFrames(Alt, 120, TOTAL_LINES, Config);

	EXPECT_NEAR(AltFrame.m_Offset, NormalFrame.m_Offset * QmScrollAltMultiplier(), 0.01f);
}

TEST(QmConsoleScroll, DragFollowsThePointerWithoutSpringDelay)
{
	CQmConsoleScrollRuntime Runtime;
	const SQmScrollConfig Config = MakeConfig();
	AdvanceFrames(Runtime, 1);
	ASSERT_GT(Runtime.MaxOffset(), 200.0f);

	// 拖动跟手：位移当帧就到位，不等弹簧。
	Runtime.DragToOffset(150.0f);
	EXPECT_FLOAT_EQ(Runtime.Offset(), 150.0f);
	Runtime.DragBy(50.0f);
	EXPECT_FLOAT_EQ(Runtime.Offset(), 200.0f);

	// 下一帧没有输入也没有动画目标，位移保持不动（若走了弹簧会被拉回或继续推进）。
	const auto Frame = AdvanceFrames(Runtime, 1);
	EXPECT_FLOAT_EQ(Frame.m_Offset, 200.0f);
	EXPECT_EQ(Frame.m_Line, 20);
	EXPECT_FLOAT_EQ(Frame.m_SubLine, 0.0f);
}

TEST(QmConsoleScroll, OverscrollDragSpringsBackIntoTheLegalRange)
{
	CQmConsoleScrollRuntime Runtime;
	const SQmScrollConfig Config = MakeConfig(3, 40.0f);
	AdvanceFrames(Runtime, 1, TOTAL_LINES, Config);
	const float MaxOffset = Runtime.MaxOffset();
	ASSERT_GT(MaxOffset, 0.0f);

	// 把内容拖到最旧一行之外：越界量按橡皮筋阻力压缩，仍允许继续拉动。
	Runtime.DragToOffset(MaxOffset + 200.0f);
	EXPECT_GT(Runtime.Offset(), MaxOffset);
	EXPECT_LE(Runtime.Offset(), MaxOffset + Config.m_MaxOverscroll + 0.001f);
	// 越界时行锚停在合法上限，越界量全部落在亚行余量上。
	const auto During = AdvanceFrames(Runtime, 1, TOTAL_LINES, Config);
	EXPECT_NEAR((float)During.m_Line * LINE_HEIGHT + During.m_SubLine, During.m_Offset, 0.001f);

	// 松手后回弹到合法范围，不留在越界区。
	const auto Frame = AdvanceFrames(Runtime, 120, TOTAL_LINES, Config);
	EXPECT_NEAR(Frame.m_Offset, MaxOffset, 0.5f);
	EXPECT_NEAR(Frame.m_SubLine, 0.0f, 0.5f);
}

TEST(QmConsoleScroll, WheelLeftInTheRubberBandZoneReturnsToTheEnd)
{
	CQmConsoleScrollRuntime Runtime;
	// 橡皮筋只在弹簧手感下由滚轮触发；顺滑档按硬边界处理（公共滚动的既有语义）。
	const SQmScrollConfig Config = MakeConfig(3, 40.0f, EQmScrollFeel::SPRING);
	AdvanceFrames(Runtime, 1, TOTAL_LINES, Config);
	const float MaxOffset = Runtime.MaxOffset();

	// 连续上滚把目标顶进橡皮筋区：允许越界，但永远不会无限越界。
	for(int i = 0; i < 40; ++i)
		Runtime.Wheel(1);
	float Peak = 0.0f;
	for(int i = 0; i < 30; ++i)
	{
		const auto Frame = Runtime.Advance(FRAME_DT, VIEWPORT, TOTAL_LINES, LINE_HEIGHT, Config);
		Peak = std::max(Peak, Frame.m_Offset);
		EXPECT_LE(Frame.m_Offset, MaxOffset + Config.m_MaxOverscroll + 0.001f);
	}
	EXPECT_GT(Peak, MaxOffset + 1.0f);

	// 停止滚动后由保持计时回收目标，弹簧把内容拉回边界。
	const auto Frame = AdvanceFrames(Runtime, 180, TOTAL_LINES, Config);
	EXPECT_NEAR(Frame.m_Offset, MaxOffset, 1.0f);
}

TEST(QmConsoleScroll, WheelWithoutBounceStopsAtTheEnd)
{
	CQmConsoleScrollRuntime Runtime;
	const SQmScrollConfig Config = MakeConfig(3, 0.0f);
	AdvanceFrames(Runtime, 1, TOTAL_LINES, Config);
	const float MaxOffset = Runtime.MaxOffset();

	// 橡皮筋关闭（qm_ui_scroll_bounce = 0，默认）时滚轮到顶就停，不留下负向或越界目标。
	for(int i = 0; i < 40; ++i)
		Runtime.Wheel(1);
	const auto Frame = AdvanceFrames(Runtime, 120, TOTAL_LINES, Config);
	EXPECT_NEAR(Frame.m_Offset, MaxOffset, 0.5f);
	EXPECT_LE(Frame.m_Offset, MaxOffset + 0.001f);
}

TEST(QmConsoleScroll, ZeroOffsetKeepsFollowingNewLines)
{
	CQmConsoleScrollRuntime Runtime;
	auto Frame = AdvanceFrames(Runtime, 1, 90);
	ASSERT_TRUE(Frame.m_Following);

	// 贴底时新日志到来：视口仍停在最新一行，不出现"锁在历史里"的漂移。
	Frame = AdvanceFrames(Runtime, 10, 95);
	EXPECT_TRUE(Frame.m_Following);
	EXPECT_EQ(Frame.m_Line, 0);
	EXPECT_FLOAT_EQ(Frame.m_Offset, 0.0f);
	EXPECT_FLOAT_EQ(Frame.m_SubLine, 0.0f);
}

TEST(QmConsoleScroll, LockedViewportStaysOnTheSameLinesWhenNewLinesArrive)
{
	CQmConsoleScrollRuntime Runtime;
	const SQmScrollConfig Config = MakeConfig();
	AdvanceFrames(Runtime, 1, 90, Config);
	Runtime.DragToOffset(20.0f * LINE_HEIGHT);
	const auto Locked = AdvanceFrames(Runtime, 1, 90, Config);
	ASSERT_FALSE(Locked.m_Following);
	ASSERT_EQ(Locked.m_Line, 20);

	// 锁定态下内容在最新端增长 5 行：偏移必须同步抬高 5 行，视口仍看原来那几行。
	const auto AfterGrowth = AdvanceFrames(Runtime, 1, 95, Config);
	EXPECT_FALSE(AfterGrowth.m_Following);
	EXPECT_EQ(AfterGrowth.m_Line, 25);
	EXPECT_NEAR(AfterGrowth.m_Offset, 25.0f * LINE_HEIGHT, 0.5f);
}

TEST(QmConsoleScroll, LineAndSubLineTogetherReproduceThePixelOffset)
{
	CQmConsoleScrollRuntime Runtime;
	// 视口高度不是行高的整数倍：最后一位的余量必须由亚行余量承载，否则内容会少滚一段。
	const SQmScrollConfig Config = MakeConfig();
	const float Viewport = 125.0f;
	Runtime.Advance(FRAME_DT, Viewport, TOTAL_LINES, LINE_HEIGHT, Config);
	Runtime.DragToOffset(Runtime.MaxOffset());
	// 同一视口高度下再推进一帧，才能对照这一帧自己的最大偏移。
	const auto Frame = Runtime.Advance(FRAME_DT, Viewport, TOTAL_LINES, LINE_HEIGHT, Config);

	EXPECT_NEAR((float)Frame.m_Line * LINE_HEIGHT + Frame.m_SubLine, Frame.m_MaxOffset, 0.001f);
	EXPECT_GE(Frame.m_SubLine, 0.0f);
	EXPECT_LT(Frame.m_SubLine, LINE_HEIGHT);
}

TEST(QmConsoleScroll, NegativeOverscrollKeepsLineAnchorAtTheNewestLine)
{
	CQmConsoleScrollRuntime Runtime;
	const SQmScrollConfig Config = MakeConfig(3, 40.0f);
	AdvanceFrames(Runtime, 1, TOTAL_LINES, Config);

	// 向最新一行之外拖动：行锚仍是 0，越界量只能落在亚行余量上（内容被往上拉）。
	Runtime.DragToOffset(-30.0f);
	const auto Frame = AdvanceFrames(Runtime, 1, TOTAL_LINES, Config);
	EXPECT_EQ(Frame.m_Line, 0);
	EXPECT_LT(Frame.m_SubLine, 0.0f);
	EXPECT_TRUE(Frame.m_Following);

	// 松手后回到 0，不会把行锚顶成负数。
	const auto Settled = AdvanceFrames(Runtime, 120, TOTAL_LINES, Config);
	EXPECT_EQ(Settled.m_Line, 0);
	EXPECT_NEAR(Settled.m_Offset, 0.0f, 0.5f);
}
