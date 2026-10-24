#include <engine/input.h>
#include <engine/keys.h>

#include <game/client/lineinput.h>

#include <gtest/gtest.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	// CLineInput 的输入设备是静态依赖，测试前后都要装回/摘掉，避免污染其它 suite。
	class CScopedLineInputInput
	{
		qm_ui_test::CTestInput m_Input;

	public:
		CScopedLineInputInput() { CLineInput::Init(nullptr, nullptr, &m_Input, nullptr); }
		~CScopedLineInputInput() { CLineInput::Init(nullptr, nullptr, nullptr, nullptr); }
		qm_ui_test::CTestInput &Input() { return m_Input; }
	};

	IInput::CEvent PressEvent(int Key)
	{
		IInput::CEvent Event{};
		Event.m_Key = Key;
		Event.m_Flags = IInput::FLAG_PRESS;
		return Event;
	}
} // namespace

TEST(LineInputMultiline, EnterInsertsNewlineAtCursor)
{
	CScopedLineInputInput Scoped;
	CLineInputBuffered<64> Line;
	Line.SetMultiline(true);
	Line.Set("ab");
	Line.SetCursorOffset(1);

	EXPECT_TRUE(Line.ProcessInput(PressEvent(KEY_RETURN)));
	EXPECT_STREQ(Line.GetString(), "a\nb");
	// 光标应停在插入的换行之后，继续输入就接在下一行。
	EXPECT_EQ(Line.GetCursorOffset(), 2u);
}

TEST(LineInputMultiline, EnterIsIgnoredUnlessMultiline)
{
	CScopedLineInputInput Scoped;
	CLineInputBuffered<64> Line;
	Line.Set("ab");

	EXPECT_FALSE(Line.ProcessInput(PressEvent(KEY_RETURN)));
	EXPECT_STREQ(Line.GetString(), "ab");
	EXPECT_EQ(Line.GetCursorOffset(), 2u);
}

TEST(LineInputMultiline, PasteKeepsLineBreaksAndNormalizesCrLf)
{
	CScopedLineInputInput Scoped;
	Scoped.Input().m_Modifier = true;
	Scoped.Input().m_Clipboard = "a\r\nb\rc";
	CLineInputBuffered<64> Line;
	Line.SetMultiline(true);
	Line.Set("");

	EXPECT_TRUE(Line.ProcessInput(PressEvent(KEY_V)));
	EXPECT_STREQ(Line.GetString(), "a\nb\nc");
}

TEST(LineInputMultiline, SingleLinePasteStillFlattensLineBreaks)
{
	CScopedLineInputInput Scoped;
	Scoped.Input().m_Modifier = true;
	Scoped.Input().m_Clipboard = "a\nb";
	CLineInputBuffered<64> Line;
	Line.Set("");

	EXPECT_TRUE(Line.ProcessInput(PressEvent(KEY_V)));
	EXPECT_STREQ(Line.GetString(), "a b");
}
