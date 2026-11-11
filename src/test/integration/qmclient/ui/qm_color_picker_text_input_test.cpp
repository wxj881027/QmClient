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

	// 需要在指定位置点击时使用：坐标是屏幕逻辑像素，转换与真实窗口一致。
	void FrameAt(vec2 Position, bool Pressed)
	{
		const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
		const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
		m_Ui.OnCursorMove(Delta.x, Delta.y);
		m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
		Frame();
	}

	// 标题栏右侧的确认按钮：位置跟随弹窗标题行布局（15px 方形按钮、右起第二枚）。
	vec2 AcceptButtonCenter() const
	{
		const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Picker);
		EXPECT_NE(pPopup, nullptr);
		const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
		const float TitleHeight = 15.0f;
		return {pPopup->x + pPopup->w - Inset - TitleHeight - 2.0f - TitleHeight * 0.5f, pPopup->y + Inset + TitleHeight * 0.5f};
	}

	// 调色板标题行右侧按钮：0=＋、1=改名、2=删除、3=撤回（与 PopupColorPicker 的右对齐布局一致）。
	vec2 PaletteHeaderButtonCenter(int SlotFromRight) const
	{
		const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Picker);
		EXPECT_NE(pPopup, nullptr);
		const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
		const float Right = pPopup->x + pPopup->w - Inset;
		const float ButtonSize = 14.0f;
		const float Gap = 4.0f;
		const float PaletteAreaHeight = 16.0f + 2.0f + 128.0f;
		const float HeaderCenterY = pPopup->y + pPopup->h - Inset - PaletteAreaHeight + 16.0f * 0.5f;
		return {Right - ButtonSize * 0.5f - SlotFromRight * (ButtonSize + Gap), HeaderCenterY};
	}

	// 用户调色板第 Index 个色块的圆心：首格是“当前颜色”，8 列均分、按行换行。
	vec2 PaletteSwatchCenter(int Index) const
	{
		const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Picker);
		EXPECT_NE(pPopup, nullptr);
		const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
		const float ContentX = pPopup->x + Inset;
		const float ContentW = pPopup->w - 2.0f * Inset;
		const float CellW = (ContentW - 7.0f * 4.0f) / 8.0f;
		const float ViewportY = pPopup->y + pPopup->h - Inset - (16.0f + 2.0f + 128.0f) + 16.0f + 2.0f;
		const int Slot = Index + 1;
		return {ContentX + (Slot % 8) * (CellW + 4.0f) + CellW * 0.5f, ViewportY + (Slot / 8) * (22.0f + 11.0f + 4.0f) + 11.0f};
	}

	void Click(const vec2 &Position)
	{
		// 热项来自上一帧渲染：先把指针移过去，再按下，否则这一次点击仍按旧位置判定。
		FrameAt(Position, false);
		FrameAt(Position, true);
		FrameAt(Position, false);
	}

	// 弹窗有开启动画：等它停稳再按坐标点击，否则内容按缩放中的位置响应。
	void Settle()
	{
		for(int i = 0; i < 40; i++)
			Frame();
	}

	void TypeText(const char *pText)
	{
		IInput::CEvent Event{};
		Event.m_Flags = IInput::FLAG_TEXT;
		str_copy(Event.m_aText, pText);
		ASSERT_TRUE(m_Ui.OnInput(Event));
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
	m_Picker.m_AlphaPercent = true;
	Open();
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
	IInput::CEvent Event{};
	Event.m_Flags = IInput::FLAG_PRESS;
	Event.m_Key = KEY_ESCAPE;
	ASSERT_TRUE(m_Ui.OnInput(Event));
	Frame();
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
	Settle();
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	ASSERT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");

	Click(PaletteSwatchCenter(0));
	ASSERT_EQ(m_Picker.m_SelectedPaletteIndex, 0);
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
	Settle();
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
	EXPECT_EQ(m_Picker.m_DraftPaletteIndex, 0);

	IInput::CEvent Event{};
	Event.m_Flags = IInput::FLAG_PRESS;
	Event.m_Key = KEY_ESCAPE;
	ASSERT_TRUE(m_Ui.OnInput(Event));
	Frame();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
}

TEST_F(QmColorPickerTextInput, RenamingDraftKeepsSwatchOnCancel)
{
	// 用户给草稿显式命名后它就是正式色块：✕ 取消不能删掉它。
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Settle();
	CLineInput &Input = m_Picker.m_aChannelInputs[0];
	Focus(Input);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Input, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);

	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(1));
	ASSERT_EQ(m_Picker.m_RenamingPaletteIndex, 0);
	Frame();
	CLineInput &Rename = m_Picker.m_RenameInput;
	ASSERT_TRUE(Rename.IsActive());
	Rename.SelectAll();
	TypeText("红");
	IInput::CEvent Enter{};
	Enter.m_Flags = IInput::FLAG_PRESS;
	Enter.m_Key = KEY_RETURN;
	ASSERT_TRUE(m_Ui.OnInput(Enter));
	Frame();
	ASSERT_EQ(m_Picker.m_RenamingPaletteIndex, -1);
	ASSERT_STREQ(m_Picker.m_vPalette[0].m_aName, "红");
	ASSERT_EQ(m_Picker.m_DraftPaletteIndex, -1);

	IInput::CEvent Escape{};
	Escape.m_Flags = IInput::FLAG_PRESS;
	Escape.m_Key = KEY_ESCAPE;
	ASSERT_TRUE(m_Ui.OnInput(Escape));
	Frame();
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "红");
}
