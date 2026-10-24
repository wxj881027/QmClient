// 真实取色器弹窗、文本输入与颜色回写协作；只替换输入设备、时钟和 GPU。
#include <engine/keys.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

class QmColorPickerTextInput : public qm_ui_test::CRealUiFixture
{
protected:
	qm_ui_test::CTestInput m_Input;
	qm_ui_test::CTestUiClient m_Client;
	CUi::SColorPickerPopupContext m_Picker;
	unsigned m_Color = 0;

	IInput *UiInput() override { return &m_Input; }
	IClient *UiClient() override { return &m_Client; }

	void Frame()
	{
		m_Client.AdvanceFrame();
		m_Ui.Update();
		m_Ui.RenderPopupMenus();
	}

	void Open(bool Alpha = true)
	{
		g_Config.m_QmColorPickerShape = CUi::SColorPickerPopupContext::SHAPE_SQUARE;
		g_Config.m_QmColorPickerPalette[0] = '\0';
		m_Picker.m_ColorMode = CUi::SColorPickerPopupContext::MODE_RGBA;
		m_Picker.m_Alpha = Alpha;
		m_Picker.m_RgbaColor = ColorRGBA(55.0f / 255.0f, 1.0f, 1.0f / 255.0f, 1.0f);
		m_Picker.m_HslaColor = color_cast<ColorHSLA>(m_Picker.m_RgbaColor);
		m_Picker.m_HsvaColor = color_cast<ColorHSVA>(m_Picker.m_HslaColor);
		m_Color = m_Picker.m_HslaColor.Pack(Alpha);
		m_Picker.m_pHslaColor = &m_Color;
		m_Ui.ShowPopupColorPicker(0.0f, 0.0f, &m_Picker);
		Frame();
		ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Picker));
	}

	void Focus(CLineInput &Input)
	{
		m_Ui.SetActiveItem(&Input);
		Frame();
		ASSERT_TRUE(Input.IsActive());
	}

	void Paste(CLineInput &Input, const char *pText)
	{
		Input.SelectAll();
		m_Input.m_Clipboard = pText;
		m_Input.m_Modifier = true;
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_PRESS;
		Event.m_Key = KEY_V;
		const bool Handled = m_Ui.OnInput(Event);
		m_Input.m_Modifier = false;
		ASSERT_TRUE(Handled);
		Frame();
	}

	void Blur(CLineInput &Input)
	{
		m_Ui.ReleaseActiveTextInput(&Input);
		Frame();
		EXPECT_FALSE(Input.IsActive());
	}
};

TEST_F(QmColorPickerTextInput, SixDigitHexPasteUpdatesColorAndSurvivesFocusLoss)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	const unsigned InitialColor = m_Color;
	Focus(m_Picker.m_HexInput);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_HexInput, "#E2D3CD");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xe2d3cdffu);
	EXPECT_NE(m_Color, InitialColor);
	EXPECT_EQ(m_Picker.m_State, EEditState::ONE_GO);
	const unsigned AppliedColor = m_Color;
	Blur(m_Picker.m_HexInput);
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#e2d3cdff");
	EXPECT_EQ(m_Color, AppliedColor);
	EXPECT_EQ(m_Picker.m_State, EEditState::NONE);
}

TEST_F(QmColorPickerTextInput, EightDigitHexPasteUpdatesAlphaAndAcceptsAnotherEdit)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Focus(m_Picker.m_HexInput);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_HexInput, "#E2D3CD80");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xe2d3cd80u);
	EXPECT_FLOAT_EQ(ColorHSLA(m_Color, true).a, 128.0f / 255.0f);
	Frame();
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xe2d3cd80u);
	Paste(m_Picker.m_HexInput, "#112233FF");
	ASSERT_FALSE(HasFatalFailure());
	Blur(m_Picker.m_HexInput);
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0x112233ffu);
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#112233ff");
}

TEST_F(QmColorPickerTextInput, IncompleteHexPreservesColorUntilTypingCompletesIt)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	const unsigned InitialColor = m_Color;
	Focus(m_Picker.m_HexInput);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_HexInput, "#E2");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(m_Color, InitialColor);
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#E2");
	IInput::CEvent Event{};
	Event.m_Flags = IInput::FLAG_TEXT;
	str_copy(Event.m_aText, "D3CD");
	ASSERT_TRUE(m_Ui.OnInput(Event));
	Frame();
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xe2d3cdffu);
	Blur(m_Picker.m_HexInput);
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#e2d3cdff");
}

TEST_F(QmColorPickerTextInput, RgbaChannelEditsUpdateColorAndSurviveFocusLoss)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	const char *apValues[] = {"226", "211", "205", "128"};
	const unsigned aExpectedColors[] = {0xe2ff01ffu, 0xe2d301ffu, 0xe2d3cdffu, 0xe2d3cd80u};
	for(int Channel = 0; Channel < 4; ++Channel)
	{
		SCOPED_TRACE(Channel);
		CLineInput &Input = m_Picker.m_aChannelInputs[Channel];
		Focus(Input);
		ASSERT_FALSE(HasFatalFailure());
		Paste(Input, apValues[Channel]);
		ASSERT_FALSE(HasFatalFailure());
		EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), aExpectedColors[Channel]);
		EXPECT_EQ(m_Picker.m_State, EEditState::ONE_GO);
		Blur(Input);
		EXPECT_STREQ(Input.GetString(), apValues[Channel]);
		EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), aExpectedColors[Channel]);
	}
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#e2d3cd80");
	EXPECT_FLOAT_EQ(ColorHSLA(m_Color, true).a, 128.0f / 255.0f);
}

TEST_F(QmColorPickerTextInput, ChannelInputClampsOutsideByteRange)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "-1");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_FLOAT_EQ(m_Picker.m_RgbaColor.r, 0.0f);
	Paste(Input, "999");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_FLOAT_EQ(m_Picker.m_RgbaColor.r, 1.0f);
	Blur(Input);
	EXPECT_STREQ(Input.GetString(), "255");
}

TEST_F(QmColorPickerTextInput, EscapeRestoresInitialColorAndReopeningAcceptsNewInput)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	const unsigned InitialColor = m_Color;
	Focus(m_Picker.m_HexInput);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_HexInput, "#E2D3CDFF");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_NE(m_Color, InitialColor);
	IInput::CEvent Event{};
	Event.m_Flags = IInput::FLAG_PRESS;
	Event.m_Key = KEY_ESCAPE;
	ASSERT_TRUE(m_Ui.OnInput(Event));
	Frame();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_EQ(m_Color, InitialColor);
	EXPECT_EQ(CLineInput::GetActiveInput(), nullptr);
	m_Ui.ShowPopupColorPicker(0.0f, 0.0f, &m_Picker);
	Frame();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#37ff01ff");
	Focus(m_Picker.m_HexInput);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_HexInput, "#112233FF");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0x112233ffu);
}

TEST_F(QmColorPickerTextInput, HexAlphaIsIgnoredWhenPickerDoesNotExposeAlpha)
{
	Open(false);
	ASSERT_FALSE(HasFatalFailure());
	Focus(m_Picker.m_HexInput);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_HexInput, "#E2D3CD80");
	ASSERT_FALSE(HasFatalFailure());
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xe2d3cdffu);
	Blur(m_Picker.m_HexInput);
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#e2d3cd");
}
