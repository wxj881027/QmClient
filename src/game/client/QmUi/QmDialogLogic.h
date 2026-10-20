#ifndef GAME_CLIENT_QMUI_QMDIALOGLOGIC_H
#define GAME_CLIENT_QMUI_QMDIALOGLOGIC_H

// 确认弹窗的键盘裁决：默认焦点停在「取消」，Enter 跟随焦点执行。
// 单独抽出纯函数，保证「危险操作不会因为随手回车而被确认」这一契约可被测试。
namespace qm_dialog
{
	inline constexpr int FOCUS_CANCEL = 0;
	inline constexpr int FOCUS_CONFIRM = 1;

	enum class EEnterOutcome
	{
		CANCEL = 0,
		CONFIRM,
	};

	inline constexpr EEnterOutcome ResolveEnter(int FocusedButton)
	{
		return FocusedButton == FOCUS_CONFIRM ? EEnterOutcome::CONFIRM : EEnterOutcome::CANCEL;
	}

	// 左右键/Tab 在两颗按钮之间来回；其它值视为取消侧，保持默认安全。
	inline constexpr int ToggleFocus(int Current)
	{
		return Current == FOCUS_CONFIRM ? FOCUS_CANCEL : FOCUS_CONFIRM;
	}

	inline constexpr int NormalizeFocus(int FocusedButton)
	{
		return FocusedButton == FOCUS_CONFIRM ? FOCUS_CONFIRM : FOCUS_CANCEL;
	}
} // namespace qm_dialog

#endif // GAME_CLIENT_QMUI_QMDIALOGLOGIC_H
