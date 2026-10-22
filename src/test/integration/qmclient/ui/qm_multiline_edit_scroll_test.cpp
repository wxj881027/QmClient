// 多行输入框的纵向滚动：长文里光标必须留在可视带内，短文本不能被平白顶起来。
// 走真实 CUi、真实文字排版与真实 lineinput，不模拟光标或行高。
#include <engine/keys.h>

#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class QmMultilineEditScroll : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CLineInputBuffered<4096> m_Field;
		// 40px 高、字号 12、行间距 3：大约只能容下两行半文字。
		CUIRect m_Rect{10.0f, 10.0f, 240.0f, 40.0f};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Draw() { m_Ui.DoEditBoxMultiLine(&m_Field, &m_Rect, 12.0f, 3.0f, TEXTALIGN_TL); }

		void DrawFrames(int Count)
		{
			for(int Index = 0; Index < Count; ++Index)
				Draw();
		}

		void Focus()
		{
			m_Ui.SetActiveItem(&m_Field);
			Draw();
			ASSERT_EQ(CLineInput::GetActiveInput(), &m_Field);
			ASSERT_TRUE(m_Field.Multiline());
		}

		void TypeText(const char *pText)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			m_Ui.OnInput(Event);
		}

		bool Key(int KeyCode)
		{
			m_Input.m_RawPressedKey = 0;
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_PRESS;
			Event.m_Key = KeyCode;
			return m_Ui.OnInput(Event);
		}
	};
}

TEST_F(QmMultilineEditScroll, CaretStaysInsideVisibleBandForLongText)
{
	m_Field.Set("");
	Focus();
	for(int Index = 0; Index < 12; ++Index)
		TypeText("line\n");
	// 平滑滚动要几帧才走完，这里显式推进帧。
	DrawFrames(120);

	EXPECT_EQ(m_Field.GetLength(), 60u);
	EXPECT_EQ(m_Field.GetCursorOffset(), 60u);
	EXPECT_GT(m_Field.GetScrollOffsetY(), 0.0f);
	const vec2 Caret = m_Field.GetCaretPosition();
	EXPECT_GE(Caret.y, m_Rect.y - 1.0f) << "scroll=" << m_Field.GetScrollOffsetY();
	EXPECT_LE(Caret.y, m_Rect.y + m_Rect.h) << "scroll=" << m_Field.GetScrollOffsetY();
}

TEST_F(QmMultilineEditScroll, ScrollingBackToTopClearsTheOffset)
{
	m_Field.Set("");
	Focus();
	for(int Index = 0; Index < 12; ++Index)
		TypeText("line\n");
	DrawFrames(120);
	ASSERT_GT(m_Field.GetScrollOffsetY(), 0.0f);

	// 回到文本开头（与真实操作一致，用 Home 键移动光标）：可见带应该重新贴回顶部。
	ASSERT_TRUE(Key(KEY_HOME));
	DrawFrames(120);
	EXPECT_LE(m_Field.GetScrollOffsetY(), 0.001f);
}

TEST_F(QmMultilineEditScroll, ShortTextNeverScrolls)
{
	m_Field.Set("one line only");
	Focus();
	TypeText("!");
	DrawFrames(30);

	EXPECT_LE(m_Field.GetScrollOffsetY(), 0.001f);
}
