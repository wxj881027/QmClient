// 服务器筛选框使用的真实 InputField、焦点会话和文本路由协作。
#include <engine/keys.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/qm_ime_manager.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_input.h>

namespace
{
	class CInputFieldGraphics : public qm_ui_test::CRealUiTestGraphics
	{
	public:
		void BlendNone() override {}
		void BlendNormal() override {}
		void BlendAdditive() override {}
		void WrapNormal() override {}
		void WrapClamp() override {}
		void QuadsDraw(CQuadItem *, int) override {}
		void SetColorVertex(const CColorVertex *, size_t) override {}
		void SetColor2(ColorRGBA, ColorRGBA) override {}
	};

	class QmImeMenuInput : public qm_ui_test::CRealUiFixture
	{
	protected:
		CInputFieldGraphics m_Graphics;
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CQmImeTextInputSession m_Session;
		CLineInputBuffered<128> m_Search;
		CLineInputBuffered<128> m_Exclude;
		CUIRect m_Rect{20.0f, 20.0f, 240.0f, 24.0f};

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }
		qm_ui_test::CRealUiTestGraphics *UiGraphics() override { return &m_Graphics; }

		void SetUp() override
		{
			CRealUiFixture::SetUp();
			if(HasFatalFailure())
				return;
			CLineInput::SetTextInputAutoManaged(true);
		}

		void BeginFrame()
		{
			m_Input.Clear();
			m_Ui.ClearHotkeys();
			m_Client.AdvanceFrame();
			m_Ui.Update();
		}

		ui_widget::SInputFieldResult Draw(CLineInput &Field, ui_widget::EInputFieldMode Mode = ui_widget::EInputFieldMode::SEARCH)
		{
			ui_widget::SInputFieldOptions Options;
			Options.m_Mode = Mode;
			Options.m_Clearable = true;
			Options.m_FontSize = 12.0f;
			return ui_widget::InputField(ui_widget::ControlContext(&m_Ui), &Field, m_Rect, Options);
		}

		void Focus(CLineInput &Field, ui_widget::EInputFieldMode Mode = ui_widget::EInputFieldMode::SEARCH)
		{
			m_Ui.SetActiveItem(&Field);
			Draw(Field, Mode);
			ASSERT_EQ(CLineInput::GetActiveInput(), &Field);
		}

		void Key(int Code, bool RawPress = true)
		{
			m_Input.m_RawPressedKey = RawPress ? Code : 0;
			IInput::CEvent Event{};
			Event.m_Key = Code;
			Event.m_Flags = IInput::FLAG_PRESS;
			m_Ui.OnInput(Event);
		}

		void Text(const char *pText)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			m_Ui.OnInput(Event);
		}
	};

	TEST_F(QmImeMenuInput, ImeConsumedEnterKeepsFocusAfterCompositionEndsBeforeDrawing)
	{
		for(const int Code : {KEY_RETURN, KEY_KP_ENTER})
		{
			SCOPED_TRACE(Code);
			BeginFrame();
			Focus(m_Search);
			m_Search.Clear();
			m_Input.m_Composing = true;
			Key(Code);
			m_Input.m_Composing = false;
			Text("中文");
			const auto Result = Draw(m_Search);
			EXPECT_TRUE(m_Search.IsActive());
			EXPECT_FALSE(Result.m_Deactivated);
			EXPECT_FALSE(Result.m_Submitted);
			EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));

			BeginFrame();
			Text("筛选");
			Draw(m_Search);
			EXPECT_STREQ(m_Search.GetString(), "中文筛选");
		}
	}

	TEST_F(QmImeMenuInput, CandidateOnlyCompositionKeepsConfirmationInIme)
	{
		BeginFrame();
		Focus(m_Search);
		m_Input.m_CandidateCount = 5;
		ASSERT_EQ(m_Input.GetCompositionLength(), 0);
		Key(KEY_RETURN);
		const auto Result = Draw(m_Search);
		EXPECT_TRUE(m_Search.IsActive());
		EXPECT_FALSE(Result.m_Submitted);
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}

	TEST_F(QmImeMenuInput, RoutedEnterSubmitsOnceWithoutRawKeyState)
	{
		BeginFrame();
		Focus(m_Search);
		Text("筛选");
		Key(KEY_RETURN, false);
		const auto Result = Draw(m_Search);
		EXPECT_TRUE(Result.m_Submitted);
		EXPECT_TRUE(Result.m_Deactivated);
		EXPECT_TRUE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
		BeginFrame();
		EXPECT_FALSE(Draw(m_Search).m_Submitted);
		EXPECT_FALSE(m_Search.IsActive());
	}

	TEST_F(QmImeMenuInput, SwitchingSearchAndExcludeRestartsSessionAndRoutesTextToNewOwner)
	{
		BeginFrame();
		Focus(m_Search);
		m_Session.UpdateFocus(m_Input, CLineInput::GetActiveInput());
		Text("搜索");
		m_Input.m_Composing = true;
		m_Input.m_CandidateCount = 3;
		Focus(m_Exclude);
		m_Session.UpdateFocus(m_Input, CLineInput::GetActiveInput());
		EXPECT_EQ(m_Input.m_TextInputStarts, 2);
		EXPECT_EQ(m_Input.m_TextInputStops, 1);
		EXPECT_FALSE(m_Input.HasComposition());
		Text("排除");
		EXPECT_STREQ(m_Search.GetString(), "搜索");
		EXPECT_STREQ(m_Exclude.GetString(), "排除");
	}

	TEST_F(QmImeMenuInput, UnrenderedPageReleasesSessionAndReopeningAcceptsText)
	{
		BeginFrame();
		Focus(m_Search);
		m_Session.UpdateFocus(m_Input, CLineInput::GetActiveInput());
		ASSERT_TRUE(CLineInput::ValidateActiveInputRenderedThisFrame());
		BeginFrame();
		EXPECT_FALSE(CLineInput::ValidateActiveInputRenderedThisFrame());
		m_Session.UpdateFocus(m_Input, CLineInput::GetActiveInput());
		EXPECT_EQ(m_Input.m_TextInputStops, 1);
		BeginFrame();
		Focus(m_Search);
		m_Session.UpdateFocus(m_Input, CLineInput::GetActiveInput());
		Text("重新输入");
		EXPECT_STREQ(m_Search.GetString(), "重新输入");
		EXPECT_EQ(m_Input.m_TextInputStarts, 2);
	}

	TEST_F(QmImeMenuInput, ClickingFocusedFieldCancelsCompositionOnceAndKeepsEditing)
	{
		for(const auto Mode : {ui_widget::EInputFieldMode::SEARCH, ui_widget::EInputFieldMode::MULTILINE})
		{
			SCOPED_TRACE(static_cast<int>(Mode));
			BeginFrame();
			m_Search.Clear();
			Focus(m_Search, Mode);
			m_Session.UpdateFocus(m_Input, CLineInput::GetActiveInput());
			const int StartsBeforeClick = m_Input.m_TextInputStarts;
			const int StopsBeforeClick = m_Input.m_TextInputStops;
			m_Input.m_Composing = true;
			m_Input.m_CandidateCount = 3;
			const vec2 Position(m_Rect.x + m_Rect.w * 0.5f, m_Rect.y + m_Rect.h * 0.5f);
			const vec2 Pixels = Position * vec2(m_Graphics.WindowWidth(), m_Graphics.WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = KEY_MOUSE_1;
			BeginFrame();
			Draw(m_Search, Mode);
			EXPECT_FALSE(m_Input.HasComposition());
			EXPECT_TRUE(m_Search.IsActive());
			EXPECT_EQ(m_Input.m_TextInputStops, StopsBeforeClick + 1);
			EXPECT_EQ(m_Input.m_TextInputStarts, StartsBeforeClick + 1);
			BeginFrame();
			Draw(m_Search, Mode);
			EXPECT_EQ(m_Input.m_TextInputStops, StopsBeforeClick + 1);
			EXPECT_EQ(m_Input.m_TextInputStarts, StartsBeforeClick + 1);
			m_Input.m_HeldKey = 0;
			BeginFrame();
			Draw(m_Search, Mode);
			Text("恢复");
			EXPECT_STREQ(m_Search.GetString(), "恢复");
		}
	}

	TEST_F(QmImeMenuInput, MultilineEnterInsertsNewlineWithoutSubmitting)
	{
		BeginFrame();
		Focus(m_Search, ui_widget::EInputFieldMode::MULTILINE);
		Text("第一行");
		Key(KEY_RETURN);
		const auto Result = Draw(m_Search, ui_widget::EInputFieldMode::MULTILINE);
		EXPECT_TRUE(m_Search.IsActive());
		EXPECT_FALSE(Result.m_Submitted);
		EXPECT_STREQ(m_Search.GetString(), "第一行\n");
	}

	TEST_F(QmImeMenuInput, ValueSelectorIgnoresImeConsumedEnterAndAcceptsNextRoutedEnter)
	{
		int Id = 0;
		SValueSelectorProperties Props;
		Props.m_UseScroll = false;
		auto DrawValue = [&] { return m_Ui.DoValueSelectorWithState(&Id, &m_Rect, "", 12, 0, 100, Props); };
		const vec2 Position(m_Rect.x + m_Rect.w * 0.5f, m_Rect.y + m_Rect.h * 0.5f);
		const vec2 Pixels = Position * vec2(m_Graphics.WindowWidth(), m_Graphics.WindowHeight()) / m_Ui.Screen()->Size();
		const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
		m_Ui.OnCursorMove(Delta.x, Delta.y);
		BeginFrame();
		DrawValue();
		m_Input.m_HeldKey = KEY_MOUSE_1;
		BeginFrame();
		DrawValue();
		m_Input.m_HeldKey = 0;
		BeginFrame();
		DrawValue();
		CLineInput *pValueInput = CLineInput::GetActiveInput();
		ASSERT_NE(pValueInput, nullptr);
		m_Input.m_Composing = true;
		Key(KEY_RETURN);
		m_Input.m_Composing = false;
		Text("34");
		EXPECT_EQ(DrawValue().m_Value, 12);
		EXPECT_EQ(CLineInput::GetActiveInput(), pValueInput);
		BeginFrame();
		Key(KEY_RETURN, false);
		EXPECT_EQ(DrawValue().m_Value, 34);
		EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
		EXPECT_FALSE(m_Ui.ConsumeHotkey(CUi::HOTKEY_ENTER));
	}
}
