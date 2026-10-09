// 真实 UI、lineinput 与文字引擎的事件/绘制协作；不模拟完整客户端或编辑器。
#include <engine/keys.h>

#include <game/client/QmUi/UiForms.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

#include <string>
#include <vector>

namespace
{
	class QmEditorTextInput : public qm_ui_test::CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		CLineInputNumber m_Number;
		CLineInputBuffered<256> m_First;
		CLineInputBuffered<256> m_Second;
		CLineInputBuffered<8> m_Small;
		CLineInputBuffered<16> m_ClipboardField;
		std::vector<std::string> m_vClipboardLines;
		CUIRect m_Rect{10.0f, 10.0f, 160.0f, 20.0f};

		IInput *UiInput() override { return &m_Input; }

		bool Draw(CLineInput &Field)
		{
			return m_Ui.DoEditBox(&Field, &m_Rect, 12.0f);
		}

		bool Draw(CLineInput &Field, bool ReleaseOnEnter)
		{
			CUi::SEditBoxRenderOptions Options;
			Options.m_ReleaseFocusOnEnter = ReleaseOnEnter;
			return m_Ui.DoEditBox(&Field, &m_Rect, 12.0f, IGraphics::CORNER_ALL, {}, TEXTALIGN_ML, Options);
		}

		void Focus(CLineInput &Field)
		{
			// 直接使用公开焦点入口，不伪造鼠标命中或编辑算法。
			m_Ui.SetActiveItem(&Field);
			Draw(Field);
			ASSERT_EQ(CLineInput::GetActiveInput(), &Field);
		}

		bool Text(const char *pText)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			return m_Ui.OnInput(Event);
		}

		void RecordClipboardLines(CLineInput &Field)
		{
			// 回调仅保存生产接口给出的文本，不复制分行或容量裁剪逻辑。
			Field.SetClipboardLineCallback([this](const char *pLine) { m_vClipboardLines.emplace_back(pLine); });
		}

		bool Paste(CLineInput &Field, const std::string &Text)
		{
			m_Input.m_Clipboard = Text;
			m_Input.m_Modifier = true;
			const bool Handled = Key(KEY_V);
			m_Input.m_Modifier = false;
			Draw(Field, false);
			return Handled;
		}

		bool Key(int KeyCode, bool RawPress = false)
		{
			m_Input.m_RawPressedKey = RawPress ? KeyCode : 0;
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_PRESS;
			Event.m_Key = KeyCode;
			return m_Ui.OnInput(Event);
		}
	};

	TEST_F(QmEditorTextInput, RetainedEnterKeepsNumberAndCallerHotkey)
	{
		m_Number.SetInteger(1);
		Focus(m_Number);
		m_Number.SelectAll();
		ASSERT_TRUE(Text("123"));
		EXPECT_TRUE(Draw(m_Number, false));
		EXPECT_EQ(m_Number.GetInteger(), 123);
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_Number, false);
		EXPECT_TRUE(m_Number.IsActive());
		EXPECT_EQ(m_Number.GetInteger(), 123);
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}

	TEST_F(QmEditorTextInput, RetainedEnterKeepsCommandTextAndCallerHotkey)
	{
		Focus(m_First);
		ASSERT_TRUE(Text("sv_name 测试"));
		EXPECT_TRUE(Draw(m_First, false));
		EXPECT_STREQ(m_First.GetString(), "sv_name 测试");
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_First, false);
		EXPECT_TRUE(m_First.IsActive());
		EXPECT_STREQ(m_First.GetString(), "sv_name 测试");
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}

	TEST_F(QmEditorTextInput, DefaultEnterReleasesActiveFieldAndPreservesOuterConfirmation)
	{
		for(int KeyCode : {KEY_RETURN, KEY_KP_ENTER})
		{
			SCOPED_TRACE(KeyCode);
			Focus(m_First);
			ASSERT_TRUE(Key(KeyCode, true));
			Draw(m_First);
			EXPECT_FALSE(m_First.IsActive());
			EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
			EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
			EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
			m_Input.Clear();
			Focus(m_Second);
			Draw(m_Second);
			EXPECT_TRUE(m_Second.IsActive());
			m_Ui.ReleaseActiveTextInput(&m_Second);
		}
	}

	TEST_F(QmEditorTextInput, InactiveFieldDoesNotStealEnterFromRetainedActiveField)
	{
		Focus(m_First);
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_Second);
		EXPECT_TRUE(m_First.IsActive());
		Draw(m_First, false);
		EXPECT_TRUE(m_First.IsActive());
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}

	TEST_F(QmEditorTextInput, InactiveFieldDoesNotConsumeDefaultActiveFieldSubmit)
	{
		Focus(m_First);
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_Second);
		EXPECT_TRUE(m_First.IsActive());
		Draw(m_First);
		EXPECT_FALSE(m_First.IsActive());
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}

	TEST_F(QmEditorTextInput, ImeConsumedEnterDoesNotReleaseDespiteRawKeyPress)
	{
		Focus(m_First);
		ASSERT_TRUE(Text("name"));
		Draw(m_First);
		m_Input.m_Composing = true;
		ASSERT_TRUE(Key(KEY_RETURN, true));
		ASSERT_TRUE(m_Input.KeyPress(KEY_RETURN));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		Draw(m_First);
		EXPECT_TRUE(m_First.IsActive());
		EXPECT_STREQ(m_First.GetString(), "name");
		m_Input.m_Composing = false;
		m_Input.Clear();
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_First);
		EXPECT_FALSE(m_First.IsActive());
	}

	TEST_F(QmEditorTextInput, SwitchingFieldsAndReopeningRoutesTextOnlyToCurrentField)
	{
		Focus(m_First);
		ASSERT_TRUE(Text("123"));
		Draw(m_First, false);
		Focus(m_Second);
		ASSERT_TRUE(Text("command"));
		Draw(m_First, false);
		Draw(m_Second, false);
		EXPECT_FALSE(m_First.IsActive());
		EXPECT_TRUE(m_Second.IsActive());
		EXPECT_STREQ(m_First.GetString(), "123");
		EXPECT_STREQ(m_Second.GetString(), "command");
		m_Ui.ReleaseActiveTextInput(&m_Second);
		EXPECT_FALSE(Text("ignored"));
		EXPECT_STREQ(m_Second.GetString(), "command");
		Focus(m_First);
		ASSERT_TRUE(Text("4"));
		Draw(m_First, false);
		EXPECT_STREQ(m_First.GetString(), "1234");
		EXPECT_STREQ(m_Second.GetString(), "command");
	}

	TEST_F(QmEditorTextInput, EmptyTextCanRetainEnterAndAcceptSubsequentTyping)
	{
		Focus(m_First);
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_First, false);
		EXPECT_TRUE(m_First.IsActive());
		EXPECT_TRUE(m_First.IsEmpty());
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		ASSERT_TRUE(Text("7"));
		EXPECT_TRUE(Draw(m_First, false));
		EXPECT_STREQ(m_First.GetString(), "7");
	}

	TEST_F(QmEditorTextInput, PasteReplacesSelectionWithinBufferAndRetainsCallerEnter)
	{
		Focus(m_Small);
		ASSERT_TRUE(Text("123"));
		Draw(m_Small, false);
		m_Small.SetCursorOffset(2);
		m_Small.SetSelection(1, 2);
		m_Input.m_Clipboard = "AB";
		m_Input.m_Modifier = true;
		ASSERT_TRUE(Key(KEY_V));
		EXPECT_TRUE(Draw(m_Small, false));
		EXPECT_STREQ(m_Small.GetString(), "1AB3");
		m_Small.SelectAll();
		m_Input.m_Clipboard = "abcdefghijk";
		ASSERT_TRUE(Key(KEY_V));
		Draw(m_Small, false);
		EXPECT_STREQ(m_Small.GetString(), "abcdefg");
		EXPECT_EQ(m_Small.GetLength(), 7u);
		EXPECT_LE(m_Small.GetCursorOffset(), m_Small.GetLength());
		m_Input.m_Modifier = false;
		ASSERT_TRUE(Key(KEY_RETURN));
		Draw(m_Small, false);
		EXPECT_TRUE(m_Small.IsActive());
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}

	TEST_F(QmEditorTextInput, MultilinePasteLimitsEveryCallbackAndKeepsLastLine)
	{
		Focus(m_Small);
		RecordClipboardLines(m_Small);
		ASSERT_TRUE(Paste(m_Small, "first\n" + std::string(4096, 'a') + "\n" + std::string(4096, 'b') + "\nlast"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"first", "aaaaaaa", "bbbbbbb"}));
		EXPECT_STREQ(m_Small.GetString(), "last");
		EXPECT_EQ(m_Small.GetCursorOffset(), 4u);
		EXPECT_TRUE(m_Small.IsActive());
	}

	TEST_F(QmEditorTextInput, MultilinePasteAppendsFirstLineBeforeCallback)
	{
		m_First.Set("pre:");
		m_First.SelectNothing();
		Focus(m_First);
		RecordClipboardLines(m_First);
		ASSERT_TRUE(Paste(m_First, "cmd\nsecond\nlast"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"pre:cmd", "second"}));
		EXPECT_STREQ(m_First.GetString(), "last");
	}

	TEST_F(QmEditorTextInput, MultilinePasteReplacesSelectionOnlyInFirstCallback)
	{
		m_First.Set("abcd");
		Focus(m_First);
		m_First.SetCursorOffset(3);
		m_First.SetSelection(1, 3);
		RecordClipboardLines(m_First);
		ASSERT_TRUE(Paste(m_First, "X\nsecond\ntail"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"aXd", "second"}));
		EXPECT_STREQ(m_First.GetString(), "tail");
		EXPECT_EQ(m_First.GetSelectionLength(), 0u);
	}

	TEST_F(QmEditorTextInput, MultilinePastePreservesEmptyCallbacksAndStripsCrLf)
	{
		Focus(m_First);
		RecordClipboardLines(m_First);
		ASSERT_TRUE(Paste(m_First, "\nfirst\r\n\nsecond\r\ntail"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"", "first", "", "second"}));
		EXPECT_STREQ(m_First.GetString(), "tail");
	}

	TEST_F(QmEditorTextInput, TrailingNewlineCallsCompletedLinesAndLeavesEmptyInput)
	{
		Focus(m_First);
		RecordClipboardLines(m_First);
		ASSERT_TRUE(Paste(m_First, "first\nsecond\n"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"first", "second"}));
		EXPECT_TRUE(m_First.IsEmpty());
		EXPECT_EQ(m_First.GetCursorOffset(), 0u);
	}

	TEST_F(QmEditorTextInput, MultilinePasteTruncatesUtf8AtWholeCharactersInCallbacksAndTail)
	{
		struct SCase
		{
			const char *m_pLine;
			const char *m_pExpected;
		};
		const SCase aCases[] = {
			{"中文中文中文中文中文中文", "中文中文中"},
			{"x中文中文中文中文", "x中文中文"},
			{"😀😀😀😀😀", "😀😀😀"},
		};
		Focus(m_ClipboardField);
		RecordClipboardLines(m_ClipboardField);
		for(const auto &Case : aCases)
		{
			SCOPED_TRACE(Case.m_pLine);
			m_ClipboardField.Clear();
			m_vClipboardLines.clear();
			ASSERT_TRUE(Paste(m_ClipboardField, std::string("first\n") + Case.m_pLine + "\n" + Case.m_pLine));
			EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"first", Case.m_pExpected}));
			EXPECT_STREQ(m_ClipboardField.GetString(), Case.m_pExpected);
			EXPECT_LE(m_ClipboardField.GetLength(), 15u);
			EXPECT_EQ(m_ClipboardField.GetCursorOffset(), m_ClipboardField.GetLength());
		}
	}

	TEST_F(QmEditorTextInput, EmptyAndSingleLinePasteDoNotCallCallbackAndNextPasteStillWorks)
	{
		m_First.Set("pre:");
		m_First.SelectNothing();
		Focus(m_First);
		RecordClipboardLines(m_First);
		ASSERT_TRUE(Paste(m_First, ""));
		EXPECT_TRUE(m_vClipboardLines.empty());
		EXPECT_STREQ(m_First.GetString(), "pre:");
		ASSERT_TRUE(Paste(m_First, "cmd"));
		EXPECT_TRUE(m_vClipboardLines.empty());
		EXPECT_STREQ(m_First.GetString(), "pre:cmd");
		ASSERT_TRUE(Paste(m_First, "\ntail"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"pre:cmd"}));
		EXPECT_STREQ(m_First.GetString(), "tail");
		ASSERT_TRUE(Paste(m_First, "X\nnext"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"pre:cmd", "tailX"}));
		EXPECT_STREQ(m_First.GetString(), "next");
	}

	TEST_F(QmEditorTextInput, RemovingClipboardCallbackRestoresSingleLinePaste)
	{
		Focus(m_First);
		RecordClipboardLines(m_First);
		ASSERT_TRUE(Paste(m_First, "first\npre:"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"first"}));
		m_First.SetClipboardLineCallback({});
		ASSERT_TRUE(Paste(m_First, "a\nb\nc"));
		EXPECT_EQ(m_vClipboardLines, (std::vector<std::string>{"first"}));
		EXPECT_STREQ(m_First.GetString(), "pre:a b c");
	}

	TEST_F(QmEditorTextInput, InputFieldLeavesFirstEnterForOuterSave)
	{
		IUiContext Ctx;
		Ctx.m_pUi = &m_Ui;
		ui_widget::SInputFieldOptions Options;
		Options.m_Clearable = false;
		for(int KeyCode : {KEY_RETURN, KEY_KP_ENTER})
		{
			SCOPED_TRACE(KeyCode);
			m_Ui.SetActiveItem(&m_First);
			ui_widget::InputField(Ctx, &m_First, m_Rect, Options);
			ASSERT_TRUE(m_First.IsActive());
			ASSERT_TRUE(Text("note"));
			ui_widget::InputField(Ctx, &m_First, m_Rect, Options);
			ASSERT_TRUE(Key(KeyCode, true));
			const auto Result = ui_widget::InputField(Ctx, &m_First, m_Rect, Options);
			EXPECT_TRUE(Result.m_Submitted);
			EXPECT_FALSE(m_First.IsActive());
			EXPECT_STREQ(m_First.GetString(), "note");
			EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
			EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
			m_Input.Clear();
			m_First.Clear();
		}
	}

	TEST_F(QmEditorTextInput, InputFieldDoesNotSubmitCompositionEnter)
	{
		IUiContext Ctx;
		Ctx.m_pUi = &m_Ui;
		ui_widget::SInputFieldOptions Options;
		Options.m_Clearable = false;
		m_Ui.SetActiveItem(&m_First);
		ui_widget::InputField(Ctx, &m_First, m_Rect, Options);
		ASSERT_TRUE(m_First.IsActive());
		m_Input.m_Composing = true;
		ASSERT_TRUE(Key(KEY_RETURN, true));
		const auto Result = ui_widget::InputField(Ctx, &m_First, m_Rect, Options);
		EXPECT_FALSE(Result.m_Submitted);
		EXPECT_TRUE(m_First.IsActive());
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		m_Input.m_Composing = false;
		m_Input.Clear();
		ASSERT_TRUE(Key(KEY_RETURN));
		ui_widget::InputField(Ctx, &m_First, m_Rect, Options);
		EXPECT_FALSE(m_First.IsActive());
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}
	TEST_F(QmEditorTextInput, MotionKeepsEnglishAndUtf8TextLayoutAndCaretAtEnd)
	{
		g_Config.m_QmUiMotionLevel = 2;
		Focus(m_First);
		for(const char *pText : {"a", "d", " 测试", "中文", "123"})
		{
			SCOPED_TRACE(pText);
			ASSERT_TRUE(Text(pText));
			const std::string Expected = m_First.GetString();
			const auto Measured = m_Ui.TextRender()->TextBoundingBox(12.0f, Expected.c_str());
			for(int Frame = 0; Frame < 3; ++Frame)
			{
				const auto Rendered = m_First.Render(&m_Rect, 12.0f, TEXTALIGN_ML, true, -1.0f, 0.0f);
				EXPECT_STREQ(m_First.GetString(), Expected.c_str());
				EXPECT_NEAR(Rendered.m_W, Measured.m_W, 0.05f);
				EXPECT_EQ(m_First.GetCursorOffset(), Expected.size());
				EXPECT_GE(m_First.GetCaretPosition().x, m_Rect.x);
				EXPECT_LE(m_First.GetCaretPosition().x, m_Rect.x + m_Rect.w);
				EXPECT_GE(m_First.GetCaretPosition().y, m_Rect.y);
				EXPECT_LE(m_First.GetCaretPosition().y, m_Rect.y + m_Rect.h);
			}
		}
	}

}
