// 弹窗/轻提示的语义契约：默认安全焦点、队列上限、锚定几何。
// 这些行为不依赖渲染上下文，按纯函数与队列状态断言，避免源码字符串合同。
#include <gtest/gtest.h>

#include <game/client/QmUi/QmDialogLogic.h>
#include <game/client/QmUi/QmPopconfirmGeometry.h>
#include <game/client/QmUi/QmToast.h>

#include <base/system.h>

namespace
{
	CUIRect MakeRect(float X, float Y, float W, float H)
	{
		CUIRect Rect;
		Rect.x = X;
		Rect.y = Y;
		Rect.w = W;
		Rect.h = H;
		return Rect;
	}
} // namespace

TEST(QmDialogLogic, EnterOnDefaultFocusCancels)
{
	// 默认焦点在取消：随手回车不确认危险操作。
	EXPECT_EQ(qm_dialog::NormalizeFocus(-1), qm_dialog::FOCUS_CANCEL);
	EXPECT_EQ(qm_dialog::NormalizeFocus(qm_dialog::FOCUS_CANCEL), qm_dialog::FOCUS_CANCEL);
	EXPECT_EQ(qm_dialog::ResolveEnter(qm_dialog::FOCUS_CANCEL), qm_dialog::EEnterOutcome::CANCEL);
}

TEST(QmDialogLogic, EnterOnConfirmFocusConfirms)
{
	EXPECT_EQ(qm_dialog::ToggleFocus(qm_dialog::FOCUS_CANCEL), qm_dialog::FOCUS_CONFIRM);
	EXPECT_EQ(qm_dialog::ResolveEnter(qm_dialog::FOCUS_CONFIRM), qm_dialog::EEnterOutcome::CONFIRM);
	// 再按一次回到取消侧，焦点不会卡在确认上。
	EXPECT_EQ(qm_dialog::ToggleFocus(qm_dialog::FOCUS_CONFIRM), qm_dialog::FOCUS_CANCEL);
}

TEST(QmToastQueue, KeepsOnlyNewestEntries)
{
	qm_ui::CToastQueue Queue;
	for(int Index = 0; Index < 5; ++Index)
	{
		char aText[16];
		str_format(aText, sizeof(aText), "toast-%d", Index);
		Queue.Push(EQmToastKind::INFO, aText, {}, 0.0);
	}
	ASSERT_EQ(Queue.Size(), qm_ui::CToastQueue::MAX_VISIBLE);
	// 丢弃最旧的两条，保留最新的三条且顺序不变。
	EXPECT_STREQ("toast-2", Queue.At(0).m_Text.c_str());
	EXPECT_STREQ("toast-4", Queue.At(Queue.Size() - 1).m_Text.c_str());
}

TEST(QmToastQueue, DuplicateTextRefreshesInsteadOfStacking)
{
	qm_ui::CToastQueue Queue;
	Queue.Push(EQmToastKind::SUCCESS, "Saved", {}, 0.0);
	Queue.Push(EQmToastKind::SUCCESS, "Saved", {}, 2.0);
	ASSERT_EQ(Queue.Size(), 1u);
	EXPECT_DOUBLE_EQ(Queue.At(0).m_BornSec, 2.0);
}

TEST(QmToastQueue, ExpiresAfterHoldPlusFadeOut)
{
	qm_ui::CToastQueue Queue;
	qm_ui::SToastOptions Options;
	Options.m_HoldSec = 1.0f;
	Queue.Push(EQmToastKind::INFO, "Done", Options, 10.0);
	Queue.Expire(10.5);
	EXPECT_EQ(Queue.Size(), 1u);
	Queue.Expire(10.0 + 1.0 + qm_ui::CToastQueue::FADE_IN_SEC + qm_ui::CToastQueue::FADE_OUT_SEC + 0.05);
	EXPECT_EQ(Queue.Size(), 0u);
}

TEST(QmToastQueue, AlphaFadesInAndOutWhileReducedMotionKeepsFullAlpha)
{
	qm_ui::CToastQueue Queue;
	qm_ui::SToastOptions Options;
	Options.m_HoldSec = 1.0f;
	Queue.Push(EQmToastKind::INFO, "Working", Options, 0.0);
	const qm_ui::CToastQueue::SEntry &Entry = Queue.At(0);
	EXPECT_LT(Queue.EntryAlpha(Entry, 0.0, 2), 0.05f);
	EXPECT_NEAR(Queue.EntryAlpha(Entry, 0.5, 2), 1.0f, 0.01f);
	EXPECT_LT(Queue.EntryAlpha(Entry, 1.0 + qm_ui::CToastQueue::FADE_OUT_SEC, 2), 0.05f);
	// 动效关闭（等级 0）时不做淡入淡出，避免出现「闪一下」。
	EXPECT_FLOAT_EQ(Queue.EntryAlpha(Entry, 1.0 + qm_ui::CToastQueue::FADE_OUT_SEC, 0), 1.0f);
}

TEST(QmPopconfirmLayout, HangsBelowAnchorRightAligned)
{
	const CUIRect Anchor = MakeRect(500.0f, 300.0f, 40.0f, 20.0f);
	const CUIRect Screen = MakeRect(0.0f, 0.0f, 1000.0f, 800.0f);
	const CUIRect Rect = qm_popconfirm_layout::ResolveRect(Anchor, 200.0f, 60.0f, Screen);
	EXPECT_FLOAT_EQ(Rect.x, 340.0f);
	EXPECT_FLOAT_EQ(Rect.y, 326.0f);
	EXPECT_FLOAT_EQ(Rect.w, 200.0f);
	EXPECT_FLOAT_EQ(Rect.h, 60.0f);
}

TEST(QmPopconfirmLayout, FlipsAboveAnchorWhenBottomIsTight)
{
	const CUIRect Anchor = MakeRect(500.0f, 740.0f, 40.0f, 20.0f);
	const CUIRect Screen = MakeRect(0.0f, 0.0f, 1000.0f, 800.0f);
	const CUIRect Rect = qm_popconfirm_layout::ResolveRect(Anchor, 200.0f, 60.0f, Screen);
	EXPECT_FLOAT_EQ(Rect.y, 740.0f - 60.0f - qm_popconfirm_layout::OFFSET);
}

TEST(QmPopconfirmLayout, ClampsInsideViewport)
{
	const CUIRect Anchor = MakeRect(10.0f, 100.0f, 40.0f, 20.0f);
	const CUIRect Screen = MakeRect(0.0f, 0.0f, 1000.0f, 800.0f);
	const CUIRect Rect = qm_popconfirm_layout::ResolveRect(Anchor, 200.0f, 60.0f, Screen);
	EXPECT_FLOAT_EQ(Rect.x, qm_popconfirm_layout::SCREEN_MARGIN);
	EXPECT_GE(Rect.y, qm_popconfirm_layout::SCREEN_MARGIN);
	EXPECT_LE(Rect.x + Rect.w, Screen.w);
}
