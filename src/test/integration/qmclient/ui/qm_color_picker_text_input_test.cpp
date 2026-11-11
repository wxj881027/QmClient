// 真实取色器弹窗、文本输入与颜色回写协作；只替换输入设备、时钟和 GPU。
#include <engine/keys.h>

#include <test/support/qm_color_picker_ui_fixture.h>

class QmColorPickerTextInput : public qm_ui_test::CColorPickerUiFixture
{
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
	const unsigned AppliedColor = m_Color;
	Blur(m_Picker.m_HexInput);
	EXPECT_STREQ(m_Picker.m_HexInput.GetString(), "#e2d3cdff");
	EXPECT_EQ(m_Color, AppliedColor);
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
	PressKey(KEY_ESCAPE);
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

TEST_F(QmColorPickerTextInput, TypedChannelValueAppliesWhileEditing)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[3];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Input.SelectAll();
	TypeText("100");
	Frame();
	// 打字过程中就应实时生效，不必等到失焦或点确认。
	EXPECT_FLOAT_EQ(m_Picker.m_RgbaColor.a, 100.0f / 255.0f);
	EXPECT_FLOAT_EQ(ColorHSLA(m_Color, true).a, 100.0f / 255.0f);
}

TEST_F(QmColorPickerTextInput, PercentAlphaTreatsTypedValueAsPercent)
{
	// 配置里是「颜色 + 不透明度百分比」时，A 通道按 0..100 工作：
	// A=100 必须是满不透明；按 0..255 解释会把 100 折算成 39%，
	// 写回百分比配置后再读出来就变成 99。
	Open(true, true);
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[3];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Input.SelectAll();
	TypeText("39");
	Frame();
	EXPECT_FLOAT_EQ(m_Picker.m_RgbaColor.a, 39.0f / 100.0f);
	Input.SelectAll();
	TypeText("100");
	Frame();
	EXPECT_FLOAT_EQ(m_Picker.m_RgbaColor.a, 1.0f);
	EXPECT_FLOAT_EQ(ColorHSLA(m_Color, true).a, 1.0f);
	Blur(Input);
	EXPECT_STREQ(Input.GetString(), "100");
}

TEST_F(QmColorPickerTextInput, EditingColorCreatesNamedDraftSwatch)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_TRUE(m_Picker.m_vPalette.empty());
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_EQ(m_Picker.m_DraftPaletteIndex, 0);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");
	EXPECT_EQ(m_Picker.m_vPalette[0].m_Color.PackAlphaLast(), m_Picker.m_RgbaColor.PackAlphaLast());
	// 继续改色只更新同一条草稿，不重复新增。
	Input.SelectAll();
	TypeText("64");
	Frame();
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_EQ(m_Picker.m_vPalette[0].m_Color.PackAlphaLast(), m_Picker.m_RgbaColor.PackAlphaLast());
}

TEST_F(QmColorPickerTextInput, CancelRemovesDraftSwatch)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
	EXPECT_EQ(m_Picker.m_DraftPaletteIndex, -1);
}

TEST_F(QmColorPickerTextInput, TypedValueSurvivesImmediateAcceptClick)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[3];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	// 先把指针停在 ✓ 上，再让「输入」和「点击」落在同一帧：
	// 关闭必须等这一帧的解析与回写跑完，否则最后这次修改会被丢掉。
	const vec2 Accept = AcceptButtonCenter();
	FrameAt(Accept, false);
	Input.SelectAll();
	TypeText("100");
	FrameAt(Accept, true);
	FrameAt(Accept, false);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_FLOAT_EQ(ColorHSLA(m_Color, true).a, 100.0f / 255.0f);
}

TEST_F(QmColorPickerTextInput, UndoAfterDeletingDraftKeepsNamesDistinct)
{
	// 「删除自定义 1 → 重新改色 → 撤回」不能在调色板里留下两个同名自动色块。
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	ASSERT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");

	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(2));
	ASSERT_TRUE(m_Picker.m_vPalette.empty());

	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Input.SelectAll();
	Paste(Input, "64");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	// 待撤回的「自定义 1」此刻不在列表里，新草稿必须避开它。
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 2");

	Click(PaletteHeaderButtonCenter(3));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "自定义 2");
}

TEST_F(QmColorPickerTextInput, CancelRemovesDraftRestoredByUndo)
{
	// 删除草稿再撤回，草稿身份要一并恢复：✕/Esc 取消仍应把它移除。
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);

	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(2));
	ASSERT_TRUE(m_Picker.m_vPalette.empty());
	Click(PaletteHeaderButtonCenter(3));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);

	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
}

TEST_F(QmColorPickerTextInput, RenamingDraftKeepsSwatchOnCancel)
{
	// 用户给草稿显式命名后它就是正式色块：✕ 取消不能删掉它。
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);

	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(1));
	Frame();
	CLineInput &Rename = m_Picker.m_RenameInput;
	ASSERT_TRUE(Rename.IsActive());
	Rename.SelectAll();
	TypeText("红");
	PressKey(KEY_RETURN);
	ASSERT_STREQ(m_Picker.m_vPalette[0].m_aName, "红");

	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "红");
}
