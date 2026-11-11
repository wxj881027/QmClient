// 取色器调色板生命周期：草稿、新增、删除/撤回、改名、容量上限与持久化。
// 走真实弹窗点击与配置串回读，不直接调用内部 lambda。
#include <engine/keys.h>

#include <test/support/qm_color_picker_ui_fixture.h>

class QmColorPickerPaletteLifecycle : public qm_ui_test::CColorPickerUiFixture
{
};

TEST_F(QmColorPickerPaletteLifecycle, AcceptPromotesDraftIntoPaletteAndConfig)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Focus(m_Picker.m_aChannelInputs[3]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[3], "100");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	ASSERT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");

	Click(AcceptButtonCenter());
	ASSERT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");
	ExpectPaletteSaved();

	// 重新打开：✓ 保留的色块是正式条目，再按 Esc 不能把它当草稿删掉。
	m_Ui.ShowPopupColorPicker(0.0f, 0.0f, &m_Picker);
	Frame();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_EQ(m_Picker.m_vPalette.size(), 1u);
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, DraftTrackingSurvivesDeleteAndUndoOfEarlierSwatch)
{
	Open(true, false, "红|#ff0000;绿|#00ff00");
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	Focus(m_Picker.m_aChannelInputs[0]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[0], "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 3u);
	ASSERT_STREQ(m_Picker.m_vPalette[2].m_aName, "自定义 1");

	// 删除草稿前面的「红」→ 草稿索引前移；撤回按原位置插回。
	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(2));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "绿");
	Click(PaletteHeaderButtonCenter(3));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 3u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "红");
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "绿");
	EXPECT_STREQ(m_Picker.m_vPalette[2].m_aName, "自定义 1");
	ExpectPaletteSaved();

	// 再删一次「红」后取消：只移除本次编辑的草稿，留下已确认的「绿」。
	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(2));
	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "绿");
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, PaletteAtCapacitySuppressesAddAndDraft)
{
	OpenWithMaxPalette();
	ASSERT_FALSE(HasFatalFailure());
	ASSERT_EQ(m_Picker.m_vPalette.size(), (size_t)qm_picker_palette::MAX_SWATCHES);

	Click(PaletteHeaderButtonCenter(0));
	EXPECT_EQ(m_Picker.m_vPalette.size(), (size_t)qm_picker_palette::MAX_SWATCHES);

	Focus(m_Picker.m_aChannelInputs[0]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[0], "128");
	EXPECT_EQ(m_Picker.m_vPalette.size(), (size_t)qm_picker_palette::MAX_SWATCHES);
	ExpectPaletteSaved();

	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_EQ(m_Picker.m_vPalette.size(), (size_t)qm_picker_palette::MAX_SWATCHES);
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, AddWithActiveDraftKeepsDraftSeparate)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Red = m_Picker.m_aChannelInputs[0];
	Focus(Red);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Red, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);

	// ＋ 保存当前颜色为正式色块：与草稿并存，不接管草稿追踪。
	Click(PaletteHeaderButtonCenter(0));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "颜色 1");
	EXPECT_EQ(m_Picker.m_vPalette[0].m_Color.PackAlphaLast(), m_Picker.m_vPalette[1].m_Color.PackAlphaLast());
	const unsigned AddedColor = m_Picker.m_vPalette[1].m_Color.PackAlphaLast();

	// 继续改色只更新草稿，刚保存的「颜色 1」保持不变。
	Focus(Red);
	ASSERT_FALSE(HasFatalFailure());
	Red.SelectAll();
	Paste(Red, "64");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "颜色 1");
	EXPECT_EQ(m_Picker.m_vPalette[1].m_Color.PackAlphaLast(), AddedColor);
	EXPECT_NE(m_Picker.m_vPalette[0].m_Color.PackAlphaLast(), AddedColor);

	// 取消只撤销草稿，＋ 确认的色块必须留下。
	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "颜色 1");
	EXPECT_EQ(m_Picker.m_vPalette[0].m_Color.PackAlphaLast(), AddedColor);
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, EmptyRenameUsesUniqueFallbackAndDetachesDraft)
{
	Open(true, false, "颜色 1|#ff0000");
	ASSERT_FALSE(HasFatalFailure());
	CLineInput &Red = m_Picker.m_aChannelInputs[0];
	Focus(Red);
	ASSERT_FALSE(HasFatalFailure());
	Paste(Red, "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	ASSERT_STREQ(m_Picker.m_vPalette[1].m_aName, "自定义 1");

	Click(PaletteSwatchCenter(1));
	Click(PaletteHeaderButtonCenter(1));
	Frame();
	CLineInput &Rename = m_Picker.m_RenameInput;
	ASSERT_TRUE(Rename.IsActive());
	ClearText(Rename);
	PressKey(KEY_RETURN);
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "颜色 1");
	// 回退名要避开已存在的「颜色 1」，不能撞名或留空。
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "颜色 2");
	const unsigned RenamedColor = m_Picker.m_vPalette[1].m_Color.PackAlphaLast();

	// 命名后它已是正式色块：再改色应新建草稿，而不是改写「颜色 2」。
	Focus(Red);
	ASSERT_FALSE(HasFatalFailure());
	Red.SelectAll();
	Paste(Red, "64");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 3u);
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "颜色 2");
	EXPECT_EQ(m_Picker.m_vPalette[1].m_Color.PackAlphaLast(), RenamedColor);
	EXPECT_STREQ(m_Picker.m_vPalette[2].m_aName, "自定义 1");

	PressKey(KEY_ESCAPE);
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "颜色 2");
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, EscapeWhileRenamingCancelsRenameOnly)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	Focus(m_Picker.m_aChannelInputs[0]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[0], "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);

	Click(PaletteSwatchCenter(0));
	Click(PaletteHeaderButtonCenter(1));
	Frame();
	CLineInput &Rename = m_Picker.m_RenameInput;
	ASSERT_TRUE(Rename.IsActive());
	Rename.SelectAll();
	TypeText("改");

	// 第一下 Esc 只取消改名，弹窗与草稿都还在。
	PressKey(KEY_ESCAPE);
	EXPECT_TRUE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "自定义 1");

	// 第二下 Esc 才是取消整次编辑：草稿随关闭移除。
	PressKey(KEY_ESCAPE);
	EXPECT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, CloseButtonCancelsDraftAndReopenReloadsConfig)
{
	Open(true, false, "红|#ff0000;绿|#00ff00");
	ASSERT_FALSE(HasFatalFailure());
	Focus(m_Picker.m_aChannelInputs[0]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[0], "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 3u);
	Click(PaletteSwatchCenter(0));

	Click(CloseButtonCenter());
	ASSERT_FALSE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 2u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "红");
	EXPECT_STREQ(m_Picker.m_vPalette[1].m_aName, "绿");
	ExpectPaletteSaved();

	// 重新打开必须重新解析配置，并清掉上一次的选中项（未选中时删除是空操作）。
	SetPaletteConfig("蓝|#0000ff;黄|#ffff00;青|#00ffff");
	m_Ui.ShowPopupColorPicker(0.0f, 0.0f, &m_Picker);
	Frame();
	ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Picker));
	ASSERT_EQ(m_Picker.m_vPalette.size(), 3u);
	EXPECT_STREQ(m_Picker.m_vPalette[0].m_aName, "蓝");
	Click(PaletteHeaderButtonCenter(2));
	EXPECT_EQ(m_Picker.m_vPalette.size(), 3u);
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, CurrentColorCellRevertsWithoutDraft)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	const unsigned InitialColor = m_Color;
	ASSERT_TRUE(m_Picker.m_vPalette.empty());

	// 首格是打开时的颜色：点它只回退工作色，不新增色块。
	Click(PaletteSwatchCenter(-1));
	EXPECT_EQ(m_Color, InitialColor);
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
	ExpectPaletteSaved();

	// 改色后再点首格：颜色回到打开时，草稿不因此多出一条。
	Focus(m_Picker.m_aChannelInputs[0]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[0], "128");
	ASSERT_EQ(m_Picker.m_vPalette.size(), 1u);
	Click(PaletteSwatchCenter(-1));
	EXPECT_EQ(m_Color, InitialColor);
	EXPECT_EQ(m_Picker.m_vPalette.size(), 1u);
}

TEST_F(QmColorPickerPaletteLifecycle, PresetClickAppliesColorWithoutDraft)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());
	const unsigned InitialColor = m_Color;

	Click(FirstPresetCenter(0));
	EXPECT_NE(m_Color, InitialColor);
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
	// 预设不可选中、不可删除。
	Click(PaletteHeaderButtonCenter(2));
	EXPECT_TRUE(m_Picker.m_vPalette.empty());
	ExpectPaletteSaved();
}

TEST_F(QmColorPickerPaletteLifecycle, ModeTabsSwitchChannelSemantics)
{
	Open();
	ASSERT_FALSE(HasFatalFailure());

	Click(ModeTabCenter(0));
	ASSERT_EQ(m_Picker.m_ColorMode, CUi::SColorPickerPopupContext::MODE_HSVA);
	Focus(m_Picker.m_aChannelInputs[0]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[0], "0");
	Focus(m_Picker.m_aChannelInputs[1]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[1], "255");
	Focus(m_Picker.m_aChannelInputs[2]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[2], "255");
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xff0000ffu);

	// HSL 的 L=255 是白色；若页签没有切换语义，仍按 HSV 解释会得到红色。
	Click(ModeTabCenter(2));
	ASSERT_EQ(m_Picker.m_ColorMode, CUi::SColorPickerPopupContext::MODE_HSLA);
	Focus(m_Picker.m_aChannelInputs[2]);
	ASSERT_FALSE(HasFatalFailure());
	Paste(m_Picker.m_aChannelInputs[2], "255");
	EXPECT_EQ(m_Picker.m_RgbaColor.PackAlphaLast(), 0xffffffffu);
}
