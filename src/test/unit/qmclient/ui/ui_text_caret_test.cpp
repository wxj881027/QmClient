// 输入光标（caret）公共组件的行为测试：三种样式的几何、闪烁相位边界与窄框夹取。
// 断言只走 qm_text_caret 的生产接口，不引用源码字符串。
#include <game/client/QmUi/UiTextCaret.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

namespace
{
	// 一个物理像素等于一个世界单位：几何断言可以直接对 Token 值，不必再换算。
	constexpr float PIXEL = 1.0f;
	constexpr float CELL_X = 40.0f;
	constexpr float CELL_Y = 12.0f;
	constexpr float CELL_W = 9.0f;
	constexpr float CELL_H = 14.0f;

	qm_text_caret::SCaretParams MakeParams(qm_text_caret::EStyle Style, double TimeSec = 0.0)
	{
		qm_text_caret::SCaretParams Params;
		Params.m_Style = Style;
		Params.m_Cell = {CELL_X, CELL_Y, CELL_W, CELL_H};
		Params.m_WorldPerPixel = PIXEL;
		Params.m_TimeSec = TimeSec;
		return Params;
	}
} // namespace

TEST(QmTextCaret, ConfigValueMapsToStyleAndClampsOutOfRange)
{
	using qm_text_caret::EStyle;
	EXPECT_EQ(qm_text_caret::StyleFromConfig(0), EStyle::BAR);
	EXPECT_EQ(qm_text_caret::StyleFromConfig(1), EStyle::UNDERSCORE);
	EXPECT_EQ(qm_text_caret::StyleFromConfig(2), EStyle::BLOCK);
	// 越界值收敛到最近的合法样式，不产生非法枚举。
	EXPECT_EQ(qm_text_caret::StyleFromConfig(-7), EStyle::BAR);
	EXPECT_EQ(qm_text_caret::StyleFromConfig(99), EStyle::BLOCK);
}

TEST(QmTextCaret, BarStyleIsThinVerticalLineAtInsertionPoint)
{
	const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(MakeParams(qm_text_caret::EStyle::BAR));
	ASSERT_TRUE(Shape.m_Visible);
	EXPECT_FLOAT_EQ(Shape.m_Rect.x, CELL_X);
	EXPECT_FLOAT_EQ(Shape.m_Rect.y, CELL_Y);
	EXPECT_FLOAT_EQ(Shape.m_Rect.w, ui_token::text_caret::THICKNESS_PX * PIXEL);
	EXPECT_FLOAT_EQ(Shape.m_Rect.h, CELL_H);
	// 竖线比字符格窄得多，读作插入点而不是选区。
	EXPECT_LT(Shape.m_Rect.w, CELL_W);
	EXPECT_FLOAT_EQ(Shape.m_Radius, ui_token::text_caret::RADIUS_PX * PIXEL);
	// 外描边沿同一条曲线向外扩一圈：四边都大出一档，圆角同步加宽。
	EXPECT_FLOAT_EQ(Shape.m_OutlineRect.x, Shape.m_Rect.x - ui_token::text_caret::OUTLINE_PX * PIXEL);
	EXPECT_FLOAT_EQ(Shape.m_OutlineRect.y, Shape.m_Rect.y - ui_token::text_caret::OUTLINE_PX * PIXEL);
	EXPECT_FLOAT_EQ(Shape.m_OutlineRect.w, Shape.m_Rect.w + 2.0f * ui_token::text_caret::OUTLINE_PX * PIXEL);
	EXPECT_FLOAT_EQ(Shape.m_OutlineRect.h, Shape.m_Rect.h + 2.0f * ui_token::text_caret::OUTLINE_PX * PIXEL);
	EXPECT_FLOAT_EQ(Shape.m_OutlineRadius, Shape.m_Radius + ui_token::text_caret::OUTLINE_PX * PIXEL);
}

TEST(QmTextCaret, UnderscoreStyleSitsOnTheBaselineAndSpansTheCharacterCell)
{
	const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(MakeParams(qm_text_caret::EStyle::UNDERSCORE));
	ASSERT_TRUE(Shape.m_Visible);
	EXPECT_FLOAT_EQ(Shape.m_Rect.x, CELL_X);
	EXPECT_FLOAT_EQ(Shape.m_Rect.w, CELL_W);
	EXPECT_FLOAT_EQ(Shape.m_Rect.h, ui_token::text_caret::THICKNESS_PX * PIXEL);
	// 下沿贴住字符格底部，不越过行高。
	EXPECT_FLOAT_EQ(Shape.m_Rect.y + Shape.m_Rect.h, CELL_Y + CELL_H);
}

TEST(QmTextCaret, BlockStyleCoversTheWholeCharacterCell)
{
	const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(MakeParams(qm_text_caret::EStyle::BLOCK));
	ASSERT_TRUE(Shape.m_Visible);
	EXPECT_FLOAT_EQ(Shape.m_Rect.x, CELL_X);
	EXPECT_FLOAT_EQ(Shape.m_Rect.y, CELL_Y);
	EXPECT_FLOAT_EQ(Shape.m_Rect.w, CELL_W);
	EXPECT_FLOAT_EQ(Shape.m_Rect.h, CELL_H);
	EXPECT_FLOAT_EQ(Shape.m_Radius, ui_token::text_caret::BLOCK_RADIUS_PX * PIXEL);
}

TEST(QmTextCaret, BlockFallsBackToFontRatioWhenCellHasNoWidth)
{
	qm_text_caret::SCaretParams Params = MakeParams(qm_text_caret::EStyle::BLOCK);
	Params.m_Cell.m_W = 0.0f;
	const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(Params);
	ASSERT_TRUE(Shape.m_Visible);
	EXPECT_FLOAT_EQ(Shape.m_Rect.w, CELL_H * ui_token::text_caret::WIDTH_RATIO);
	EXPECT_FLOAT_EQ(Shape.m_Rect.h, CELL_H);
}

TEST(QmTextCaret, PixelScaleMultipliesEveryTokenDerivedDimension)
{
	qm_text_caret::SCaretParams Params = MakeParams(qm_text_caret::EStyle::BAR);
	Params.m_WorldPerPixel = 2.0f;
	const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(Params);
	ASSERT_TRUE(Shape.m_Visible);
	EXPECT_FLOAT_EQ(Shape.m_Rect.w, ui_token::text_caret::THICKNESS_PX * 2.0f);
	EXPECT_FLOAT_EQ(Shape.m_Rect.h, CELL_H);
	EXPECT_FLOAT_EQ(Shape.m_PixelSize, 2.0f);
	// 圆角仍不超过最短边的一半。
	EXPECT_LE(Shape.m_Radius, Shape.m_Rect.w * 0.5f);
}

TEST(QmTextCaret, BlinkPhaseBoundariesFollowTheMotionTokens)
{
	const double Period = ui_token::motion::CARET_BLINK_PERIOD_SEC;
	const double Visible = Period * ui_token::motion::CARET_BLINK_DUTY;
	ASSERT_GT(Period, 0.0);
	ASSERT_GT(Visible, 0.0);

	EXPECT_TRUE(qm_text_caret::BlinkVisible(0.0, true));
	EXPECT_TRUE(qm_text_caret::BlinkVisible(Visible * 0.99, true));
	// 亮相时长到点即转入灭相（边界取「小于」，不重叠也不漏一帧）。
	EXPECT_FALSE(qm_text_caret::BlinkVisible(Visible, true));
	EXPECT_FALSE(qm_text_caret::BlinkVisible(Period * 0.99, true));
	// 跨过整个周期后重新亮相，相位按周期回绕。
	EXPECT_TRUE(qm_text_caret::BlinkVisible(Period, true));
	EXPECT_TRUE(qm_text_caret::BlinkVisible(Period + Visible * 0.5, true));
	EXPECT_FALSE(qm_text_caret::BlinkVisible(Period * 3.0 + Visible, true));
}

TEST(QmTextCaret, BlinkDisabledKeepsTheCaretSolidAtAnyTime)
{
	const double Period = ui_token::motion::CARET_BLINK_PERIOD_SEC;
	qm_text_caret::SCaretParams Params = MakeParams(qm_text_caret::EStyle::BAR, Period * 0.75);
	Params.m_Blink = false;
	EXPECT_TRUE(qm_text_caret::ResolveShape(Params).m_Visible);
	// 同一时刻按周期开启闪烁时，恰好处于灭相：两者必须不同，证明开关真的生效。
	Params.m_Blink = true;
	EXPECT_FALSE(qm_text_caret::ResolveShape(Params).m_Visible);
}

TEST(QmTextCaret, DifferentInputFieldsShareTheSameBlinkPhase)
{
	const double Period = ui_token::motion::CARET_BLINK_PERIOD_SEC;
	const double TimeSec = Period * 0.75;
	qm_text_caret::SCaretParams First = MakeParams(qm_text_caret::EStyle::BAR, TimeSec);
	qm_text_caret::SCaretParams Second = MakeParams(qm_text_caret::EStyle::BLOCK, TimeSec);
	Second.m_Cell.m_X += 120.0f;
	Second.m_Cell.m_Y += 30.0f;

	// 相位只取决于时间，与光标位置、样式无关：并排的两个输入框同时亮同时灭。
	EXPECT_FALSE(qm_text_caret::ResolveShape(First).m_Visible);
	EXPECT_EQ(qm_text_caret::ResolveShape(First).m_Visible, qm_text_caret::ResolveShape(Second).m_Visible);

	// 换到亮相段后两个输入框一起亮，说明可见性不是被位置或样式带偏的。
	First.m_TimeSec = Period * 0.25;
	Second.m_TimeSec = Period * 0.25;
	EXPECT_TRUE(qm_text_caret::ResolveShape(First).m_Visible);
	EXPECT_EQ(qm_text_caret::ResolveShape(First).m_Visible, qm_text_caret::ResolveShape(Second).m_Visible);

	// 推进整整一个周期后可见性不变（相位按周期回绕）。
	First.m_TimeSec = TimeSec + Period;
	Second.m_TimeSec = TimeSec + Period;
	EXPECT_EQ(qm_text_caret::ResolveShape(First).m_Visible, qm_text_caret::ResolveShape(Second).m_Visible);
}

TEST(QmTextCaret, ZeroHeightCellDrawsNothing)
{
	qm_text_caret::SCaretParams Params = MakeParams(qm_text_caret::EStyle::BLOCK);
	Params.m_Cell.m_H = 0.0f;
	EXPECT_FALSE(qm_text_caret::ResolveShape(Params).m_Visible);
	Params.m_Cell.m_H = -3.0f;
	EXPECT_FALSE(qm_text_caret::ResolveShape(Params).m_Visible);
}

TEST(QmTextCaret, BlockCaretIsClippedToANarrowField)
{
	qm_text_caret::SCaretParams Params = MakeParams(qm_text_caret::EStyle::BLOCK);
	Params.m_HorizBounds = {CELL_X, CELL_Y, 4.0f, CELL_H};
	const qm_text_caret::SCaretShape Shape = qm_text_caret::ResolveShape(Params);
	ASSERT_TRUE(Shape.m_Visible);
	// 可用宽度不足一格时先收窄再贴住左边界，整块留在文字区内。
	EXPECT_FLOAT_EQ(Shape.m_Rect.w, Params.m_HorizBounds.w);
	EXPECT_FLOAT_EQ(Shape.m_Rect.x, Params.m_HorizBounds.x);
	EXPECT_GE(Shape.m_Rect.x, Params.m_HorizBounds.x);
	EXPECT_LE(Shape.m_Rect.x + Shape.m_Rect.w, Params.m_HorizBounds.x + Params.m_HorizBounds.w + 0.001f);
	// 外描边一起夹住，窄框里不会多画出框外那一圈。
	EXPECT_GE(Shape.m_OutlineRect.x, Params.m_HorizBounds.x);
	EXPECT_LE(Shape.m_OutlineRect.x + Shape.m_OutlineRect.w, Params.m_HorizBounds.x + Params.m_HorizBounds.w + 0.001f);
}

TEST(QmTextCaret, CaretAtTheRightEdgeIsShiftedInsideTheField)
{
	const CUIRect Bounds{CELL_X, CELL_Y, 20.0f, CELL_H};
	const float BoundsRight = Bounds.x + Bounds.w;

	qm_text_caret::SCaretParams Block = MakeParams(qm_text_caret::EStyle::BLOCK);
	Block.m_Cell.m_X = BoundsRight - 1.0f;
	Block.m_HorizBounds = Bounds;
	const qm_text_caret::SCaretShape BlockShape = qm_text_caret::ResolveShape(Block);
	EXPECT_FLOAT_EQ(BlockShape.m_Rect.x + BlockShape.m_Rect.w, BoundsRight);

	qm_text_caret::SCaretParams Bar = MakeParams(qm_text_caret::EStyle::BAR);
	Bar.m_Cell.m_X = BoundsRight - 0.1f;
	Bar.m_HorizBounds = Bounds;
	const qm_text_caret::SCaretShape BarShape = qm_text_caret::ResolveShape(Bar);
	EXPECT_LE(BarShape.m_Rect.x + BarShape.m_Rect.w, BoundsRight + 0.001f);
	EXPECT_FLOAT_EQ(BarShape.m_Rect.w, ui_token::text_caret::THICKNESS_PX * PIXEL);
}
