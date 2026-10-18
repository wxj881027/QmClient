// 真实组合框、输入和弹层协作；只替换输入设备、客户端时钟与 GPU。
#include <engine/keys.h>

#include <game/client/ui_scrollregion.h>

#include <test/support/qm_anim_test_helpers.h>
#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

class QmFontCombobox : public qm_ui_test::CRealUiFixture
{
protected:
	qm_ui_test::CTestInput m_Input;
	qm_ui_test::CTestUiClient m_Client;
	CUi::SDropDownState m_State;
	CUIRect m_Rect{10, 10, 240, 24};
	std::vector<const char *> m_vNames{"Default", "Arial", "思源黑体", "Arial Narrow"};
	int m_Selected = 0;
	IInput *UiInput() override { return &m_Input; }
	IClient *UiClient() override { return &m_Client; }

	void Frame()
	{
		m_Client.AdvanceFrame();
		m_Ui.Update();
		CUi::SDropDownProperties Props;
		Props.m_Searchable = true;
		m_Selected = m_Ui.DoDropDown(&m_Rect, m_Selected, m_vNames.data(), m_vNames.size(), m_State, Props);
		m_Ui.RenderPopupMenus();
	}
	void Open()
	{
		m_Ui.OnCursorMove((m_Rect.x + 10.0f - m_Ui.MouseX()) * m_Ui.Graphics()->WindowWidth() / m_Ui.Screen()->w,
			(m_Rect.y + 10.0f - m_Ui.MouseY()) * m_Ui.Graphics()->WindowHeight() / m_Ui.Screen()->h);
		Frame();
		Frame();
		m_Input.m_HeldKey = KEY_MOUSE_1;
		Frame();
		m_Input.m_HeldKey = 0;
		Frame();
		ASSERT_TRUE(m_Ui.IsPopupOpen(&m_State.m_SelectionPopupContext));
		ASSERT_EQ(CLineInput::GetActiveInput(), &m_State.m_SelectionPopupContext.m_SearchInput);
	}
	void Text(const char *pText)
	{
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_TEXT;
		str_copy(Event.m_aText, pText);
		ASSERT_TRUE(m_Ui.OnInput(Event));
	}
	void Key(int Code)
	{
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_PRESS;
		Event.m_Key = Code;
		m_Ui.OnInput(Event);
	}
};

TEST_F(QmFontCombobox, TypedFilterAndKeyboardCommitUseOriginalFontIndex)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Text("arial");
	Frame();
	EXPECT_EQ(m_Selected, 0);
	EXPECT_EQ(m_State.m_SelectionPopupContext.m_Search.Indices(), (std::vector<int>{1, 3}));
	Key(KEY_DOWN);
	Frame();
	Key(KEY_RETURN);
	Frame();
	Frame();
	EXPECT_EQ(m_Selected, 3);
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmFontCombobox, NoMatchEnterPreservesSelectionAndEscapeClearsInputOnReopen)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Text("missing-font");
	Frame();
	Key(KEY_RETURN);
	Frame();
	EXPECT_TRUE(m_Ui.IsPopupOpen());
	EXPECT_EQ(m_Selected, 0);
	EXPECT_EQ(CLineInput::GetActiveInput(), &m_State.m_SelectionPopupContext.m_SearchInput);
	Key(KEY_ESCAPE);
	Frame();
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
	Open();
	EXPECT_STREQ(m_State.m_SelectionPopupContext.m_SearchInput.GetString(), "");
	EXPECT_EQ(m_State.m_SelectionPopupContext.m_Search.Indices().size(), m_vNames.size());
}

TEST_F(QmFontCombobox, ImeEnterDoesNotCommitFontAndUnicodeQueryFindsCandidate)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	m_Input.m_Composing = true;
	Key(KEY_RETURN);
	Frame();
	EXPECT_TRUE(m_Ui.IsPopupOpen());
	EXPECT_EQ(m_Selected, 0);
	m_Input.m_Composing = false;
	Text("黑体");
	Frame();
	EXPECT_EQ(m_State.m_SelectionPopupContext.m_Search.Indices(), (std::vector<int>{2}));
	Key(KEY_RETURN);
	Frame();
	Frame();
	EXPECT_EQ(m_Selected, 2);
}

TEST_F(QmFontCombobox, CandidateSourceChangeCancelsOpenSearchWithoutApplyingStaleIndex)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Text("arial");
	Frame();
	m_vNames = {"Default", "思源黑体"};
	Frame();
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_EQ(m_Selected, 0);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmFontCombobox, ClickingOutsideCancelsDraftAndReleasesSearchFocus)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Text("Arial");
	Frame();
	m_Ui.OnCursorMove(1600.0f, 900.0f);
	m_Input.m_HeldKey = KEY_MOUSE_1;
	Frame();
	m_Input.m_HeldKey = 0;
	Frame();
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	EXPECT_EQ(m_Selected, 0);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
}

TEST_F(QmFontCombobox, ClosedSelectorDoesNotActivateTextInput)
{
	Frame();
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
	EXPECT_FALSE(m_Ui.IsPopupOpen());
	Open();
	EXPECT_EQ(CLineInput::GetActiveInput(), &m_State.m_SelectionPopupContext.m_SearchInput);
}

TEST_F(QmFontCombobox, AnimatedCommitResetsContextBeforeExitAndCanReopen)
{
	CScopedQmFullMotion FullMotion;
	CScrollRegion ExternalScrollRegion;
	Open();
	ASSERT_FALSE(HasFatalFailure());
	for(int Index = 0; Index < 20; ++Index)
		Frame();
	Key(KEY_DOWN);
	Frame();
	Key(KEY_RETURN);
	Frame();
	ASSERT_FALSE(m_Ui.IsPopupOpen());
	ASSERT_TRUE(m_Ui.IsPopupVisible(&m_State.m_SelectionPopupContext));
	Frame();
	EXPECT_EQ(m_Selected, 1);
	EXPECT_EQ(m_State.m_SelectionPopupContext.m_pUI, nullptr);
	// 图形设置页每帧重绑外部滚动区；这会绕过旧回调仅检查滚动区的空指针兜底。
	m_State.m_SelectionPopupContext.m_pScrollRegion = &ExternalScrollRegion;
	Frame();
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
	for(int Index = 0; Index < 20; ++Index)
		Frame();
	EXPECT_FALSE(m_Ui.IsPopupVisible(&m_State.m_SelectionPopupContext));
	Open();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_State.m_SelectionPopupContext));
}

TEST_F(QmFontCombobox, AnimatedEscapeKeepsSelectionAndClearsDraftOnReopen)
{
	CScopedQmFullMotion FullMotion;
	Open();
	ASSERT_FALSE(HasFatalFailure());
	for(int Index = 0; Index < 20; ++Index)
		Frame();
	Text("Arial");
	Frame();
	Key(KEY_ESCAPE);
	Frame();
	ASSERT_FALSE(m_Ui.IsPopupOpen());
	ASSERT_TRUE(m_Ui.IsPopupVisible(&m_State.m_SelectionPopupContext));
	Frame();
	EXPECT_EQ(m_Selected, 0);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
	for(int Index = 0; Index < 20; ++Index)
		Frame();
	Open();
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_STREQ(m_State.m_SelectionPopupContext.m_SearchInput.GetString(), "");
}
