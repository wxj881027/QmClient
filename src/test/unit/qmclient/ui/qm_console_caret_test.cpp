// 控制台光标的行为测试：闪烁门控（拖选/关闭动效恒亮）与"相位走全局时间"的时钟来源。
// 断言只走 qm_text_caret 的生产接口，不引用源码字符串。
#include <game/client/QmUi/UiTextCaret.h>
#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

namespace
{
	qm_text_caret::SCaretParams MakeParams(double TimeSec, bool Blink = true)
	{
		qm_text_caret::SCaretParams Params;
		Params.m_Style = qm_text_caret::EStyle::BAR;
		Params.m_Cell = {40.0f, 12.0f, 0.0f, 14.0f};
		Params.m_WorldPerPixel = 1.0f;
		Params.m_TimeSec = TimeSec;
		Params.m_Blink = Blink;
		return Params;
	}
} // namespace

TEST(QmConsoleCaret, NormalInputBlinksSoTheConsoleCaretIsNotHardcodedSolid)
{
	// 常规输入行的光标必须参与闪烁；写死 false 会让控制台光标永远常亮。
	EXPECT_TRUE(qm_text_caret::ShouldBlink(true, 2));
	EXPECT_TRUE(qm_text_caret::ShouldBlink(true, 1));

	// 同一门控下，亮相段与灭相段的可见性必须不同：证明相位真的在走。
	const double Period = ui_token::motion::CARET_BLINK_PERIOD_SEC;
	const bool Blink = qm_text_caret::ShouldBlink(true, 2);
	EXPECT_TRUE(qm_text_caret::ResolveShape(MakeParams(Period * 0.25, Blink)).m_Visible);
	EXPECT_FALSE(qm_text_caret::ResolveShape(MakeParams(Period * 0.75, Blink)).m_Visible);
}

TEST(QmConsoleCaret, MotionLevelZeroKeepsTheCaretSolidAsTheOnlyException)
{
	// qm_ui_motion_level = 0 是全站唯一"关掉动效反而恒亮"的例外：光标取亮相终点。
	EXPECT_FALSE(qm_text_caret::ShouldBlink(true, 0));
	const double Period = ui_token::motion::CARET_BLINK_PERIOD_SEC;
	// 灭相时刻也因为恒亮而可见。
	EXPECT_TRUE(qm_text_caret::ResolveShape(MakeParams(Period * 0.75, qm_text_caret::ShouldBlink(true, 0))).m_Visible);
	// 同一时刻开启闪烁则恰好灭相，证明恒亮来自门控而不是几何。
	EXPECT_FALSE(qm_text_caret::ResolveShape(MakeParams(Period * 0.75, qm_text_caret::ShouldBlink(true, 2))).m_Visible);
}

TEST(QmConsoleCaret, SelectionKeepsTheConsoleCaretSolidWhileDragging)
{
	// 拖选、输入法组合期间光标不该闪（与 CLineInput 同一口径）。
	EXPECT_FALSE(qm_text_caret::ShouldBlink(false, 2));
	EXPECT_FALSE(qm_text_caret::ShouldBlink(false, 0));
}

TEST(QmConsoleCaret, GlobalClockAdvancesSoThePhaseIsNotStatic)
{
	// 相位时间每帧现取：连续采样必须推进，否则控制台光标又回到"时间恒为 0 所以常亮"。
	const double First = qm_text_caret::NowSeconds();
	double Second = First;
	for(int i = 0; i < 1000 && Second <= First; ++i)
		Second = qm_text_caret::NowSeconds();
	EXPECT_GT(Second, First);
}
