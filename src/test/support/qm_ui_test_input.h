// 仅模拟输入设备状态与剪贴板，不实现文本编辑或热键路由。
#ifndef TEST_SUPPORT_QM_UI_TEST_INPUT_H
#define TEST_SUPPORT_QM_UI_TEST_INPUT_H

#include <engine/input.h>

namespace qm_ui_test
{
	class CTestInput final : public IInput
	{
		std::vector<CTouchFingerState> m_vTouch;

	public:
		int m_RawPressedKey = 0;
		int m_HeldKey = 0;
		bool m_Modifier = false;
		bool m_Composing = false;
		std::string m_Clipboard;

		void ConsumeEvents(std::function<void(const CEvent &)>) const override {}
		void Clear() override { m_RawPressedKey = 0; }
		float GetUpdateTime() const override { return 0.0f; }
		bool ModifierIsPressed() const override { return m_Modifier; }
		bool ShiftIsPressed() const override { return false; }
		bool AltIsPressed() const override { return false; }
		bool KeyIsPressed(int Key) const override { return m_HeldKey != 0 && Key == m_HeldKey; }
		bool KeyPress(int Key) const override { return m_RawPressedKey != 0 && Key == m_RawPressedKey; }
		const char *KeyName(int) const override { return ""; }
		int FindKeyByName(const char *) const override { return 0; }
		size_t NumJoysticks() const override { return 0; }
		IJoystick *GetJoystick(size_t) override { return nullptr; }
		IJoystick *GetActiveJoystick() override { return nullptr; }
		void SetActiveJoystick(size_t) override {}
		vec2 NativeMousePos() const override { return vec2(0.0f, 0.0f); }
		bool NativeMousePressed(int) const override { return false; }
		void MouseModeRelative() override {}
		void MouseModeAbsolute() override {}
		bool MouseRelative(float *, float *) override { return false; }
		const std::vector<CTouchFingerState> &TouchFingerStates() const override { return m_vTouch; }
		void ClearTouchDeltas() override {}
		std::string GetClipboardText() override { return m_Clipboard; }
		void SetClipboardText(const char *pText) override { m_Clipboard = pText; }
		void StartTextInput() override {}
		void StopTextInput() override {}
		void EnsureScreenKeyboardShown() override {}
		const char *GetComposition() const override { return m_Composing ? "x" : ""; }
		bool HasComposition() const override { return m_Composing; }
		int GetCompositionCursor() const override { return 0; }
		int GetCompositionLength() const override { return m_Composing ? 1 : 0; }
		const char *GetCandidate(int) const override { return ""; }
		int GetCandidateCount() const override { return 0; }
		int GetCandidateSelectedIndex() const override { return -1; }
		int GetCandidatePageStart() const override { return 0; }
		int GetCandidatePageSize() const override { return 0; }
		int GetCandidateTotalCount() const override { return 0; }
		void SetCompositionWindowPosition(float, float, float) override {}
		bool GetDropFile(char *, int) override { return false; }
	};
}

#endif
