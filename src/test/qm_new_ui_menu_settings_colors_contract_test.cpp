// QmNewUi 菜单源码合同：设置外观颜色域：颜色选择器独立 alpha、Qm 本地化色标、默认表面不透明度。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

TEST(QmNewUiMenuSettingsColorsContract, NewSettingsUseToggleAndExposeAccentAndBlurControls)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(ConfigSource.find("QmUiAccentColor, qm_ui_accent_color"), std::string::npos);
	EXPECT_NE(ConfigSource.find("QmUiSelectedColor, qm_ui_selected_color"), std::string::npos);
	const std::string SettingsCheckbox = FunctionBody(MenusSource, "int CMenus::DoSettingsButton_CheckBox(int Page, int Tab, int Subtab, const void *pId, const char *pTextId, const char *pText, int Checked, const CUIRect *pRect, const SLabelProperties &LabelProps, const bool ProcessInput, const float RequestedFontSize)");
	EXPECT_NE(SettingsCheckbox.find("if(g_Config.m_QmNewUi)"), std::string::npos);
	EXPECT_NE(SettingsCheckbox.find("ui_widget::Toggle(Context, pId, &ToggleValue, ToggleRect, false, ProcessInput)"), std::string::npos);
	EXPECT_NE(SettingsCheckbox.find("Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_LEFT)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Interface accent color\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Selected item color\")"), std::string::npos);
	// 高斯模糊开关的稳定文案与稳定 ID（旧的 "Enable Gaussian blur" 已随渲染模式统一移除）。
	EXPECT_NE(SettingsSource.find("DoSettingsButton_CheckBox(SETTINGS_GRAPHICS, -1, &g_Config.m_QmGaussianBlur, \"enable-backdrop-blur\", Localize(\"Enable backdrop blur\"), g_Config.m_QmGaussianBlur, &Button)"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Blur mode\")"), std::string::npos);
	EXPECT_EQ(SettingsSource.find("Localize(\"Enable Gaussian blur\")"), std::string::npos);
}

TEST(QmNewUiMenuSettingsColorsContract, SettingsColorLabelsUseQmLocalizedKeys)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string MenusToml = ReadTextFile("qmclient_scripts/languages_qmclient/translations/i18n/menus.toml");

	EXPECT_EQ(Source.find("Localize(\"UI Color\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel color\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Menu panel elevated opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("s_MenuPanelColorResetId"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_ClMenuPanelColor"), std::string::npos);
	EXPECT_EQ(Source.find("g_Config.m_UiColor"), std::string::npos);
	EXPECT_NE(Source.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect UiColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize(\"Interface surface\"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect MapBrowserColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_MapBrowserColorResetId, ColorMetrics, &MapBrowserColorRow, Localize(\"Map browser surface\"), &g_Config.m_QmMapBrowserColor, &g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect ScoreboardColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Source.find("DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize(\"Scoreboard surface\"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmUiColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardColor"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Interface surface\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Map browser surface\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Scoreboard surface\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"UI opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Map browser opacity\")"), std::string::npos);
	EXPECT_EQ(Source.find("Localize(\"Scoreboard opacity\")"), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"UI opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"界面不透明度\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Map browser opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"地图浏览器不透明度\""), std::string::npos);
	EXPECT_NE(MenusToml.find("key = \"Scoreboard opacity\""), std::string::npos);
	EXPECT_NE(MenusToml.find("simplified_chinese = \"计分板不透明度\""), std::string::npos);
}

TEST(QmNewUiMenuSettingsColorsContract, SettingsGraphicsColorPickersExposeIndependentAlphaDomains)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Graphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	ASSERT_FALSE(Graphics.empty());

	EXPECT_NE(Graphics.find("CSettingsContentRowFlow Rows(ContentRect, GraphicsMetrics);"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect UiColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_UiColorResetId, ColorMetrics, &UiColorRow, Localize(\"Interface surface\"), &g_Config.m_QmUiColor, &g_Config.m_QmUiOpacity"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect MapBrowserColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_MapBrowserColorResetId, ColorMetrics, &MapBrowserColorRow, Localize(\"Map browser surface\"), &g_Config.m_QmMapBrowserColor, &g_Config.m_QmMapBrowserOpacity"), std::string::npos);
	EXPECT_NE(Graphics.find("CUIRect ScoreboardColorRow = Rows.NextButton();"), std::string::npos);
	EXPECT_NE(Graphics.find("DoLine_AlphaColorPicker(&s_ScoreboardColorResetId, ColorMetrics, &ScoreboardColorRow, Localize(\"Scoreboard surface\"), &g_Config.m_QmScoreboardColor, &g_Config.m_QmScoreboardOpacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-ui-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-map-browser-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("graphics-scoreboard-opacity"), std::string::npos);
	EXPECT_EQ(Graphics.find("DoSliderWithValueInput("), std::string::npos);
}

TEST(QmNewUiMenuSettingsColorsContract, DynamicIslandColorPickerOwnsExistingOpacitySetting)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_NE(Body.find("DoLine_AlphaColorPicker(&s_DynamicIslandBgColorId, CurrentSettingsContentMetrics(), &Content, Localize(\"Background color\"), &g_Config.m_QmHudIslandBgColor, &g_Config.m_QmHudIslandBgOpacity, 0x9C460E, 80)"), std::string::npos);
	EXPECT_EQ(Body.find("s_QmHudIslandBgOpacityInputId"), std::string::npos);
	EXPECT_EQ(Body.find("RenderQmSettingsSliderWithValueInput(&s_QmHudIslandBgOpacityInputId"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_COL(QmHudIslandBgColor, qm_hud_island_bg_color, 0x9C460E, CFGFLAG_CLIENT | CFGFLAG_SAVE"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_INT(QmHudIslandBgOpacity, qm_hud_island_bg_opacity, 80"), std::string::npos);
}

TEST(QmNewUiMenuSettingsColorsContract, TranslateUiColorsPreserveConfiguredAlpha)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	for(const char *pKey : {
		    "MACRO_CONFIG_COL(QmTranslateBtnColorDisabled, qm_translate_btn_color_disabled, 0xD1000029, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateBtnColorEnabled, qm_translate_btn_color_enabled, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuBgColor, qm_translate_menu_bg_color, 0xF200001F, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuOptionSelected, qm_translate_menu_option_selected, 0xE69E5E86, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
		    "MACRO_CONFIG_COL(QmTranslateMenuOptionNormal, qm_translate_menu_option_normal, 0xE6000033, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA",
	    })
		EXPECT_NE(Config.find(pKey), std::string::npos) << pKey;
}

TEST(QmNewUiMenuSettingsColorsContract, DefaultUiSurfacesUseBlackThirtyPercent)
{
	const std::string QmConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables.h");

	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmUiColor, qm_ui_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmMapBrowserColor, qm_map_browser_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_COL(QmScoreboardColor, qm_scoreboard_color, 0x000000"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmUiOpacity, qm_ui_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmUiCardOpacity, qm_ui_card_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmMapBrowserOpacity, qm_map_browser_opacity, 30"), std::string::npos);
	EXPECT_NE(QmConfigSource.find("MACRO_CONFIG_INT(QmScoreboardOpacity, qm_scoreboard_opacity, 30"), std::string::npos);

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(UiColor, ui_color, 0x4D000000"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_COL(ClMenuPanelColor, cl_menu_panel_color, 0x000000"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClMenuPanelOpacity, cl_menu_panel_opacity, 30"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClMenuPanelElevatedOpacity, cl_menu_panel_elevated_opacity, 30"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(ClSettingsTabbarOpacity, cl_settings_tabbar_opacity, 30"), std::string::npos);
}

TEST(QmNewUiMenuSettingsColorsContract, GeneralSettingsListsShareSelectedAndHoveredBackgroundTokens)
{
	const std::string Tokens = ReadTextFile("src/game/client/QmUi/UiTokens.h");
	const std::string ListboxHeader = ReadTextFile("src/game/client/ui_listbox.h");
	const std::string Listbox = ReadTextFile("src/game/client/ui_listbox.cpp");
	const std::string Menus = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string Settings = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string Theme = FunctionBody(Menus, "void CMenus::RenderThemeSelection(");
	const std::string Language = FunctionBody(Settings, "bool CMenus::RenderLanguageSelection(");
	ASSERT_FALSE(Theme.empty());
	ASSERT_FALSE(Language.empty());

	EXPECT_NE(Tokens.find("LIST_ITEM_SELECTED{1.0f, 1.0f, 1.0f, 0.14f}"), std::string::npos);
	EXPECT_NE(Tokens.find("LIST_ITEM_HOVER{1.0f, 1.0f, 1.0f, 0.08f}"), std::string::npos);
	EXPECT_NE(ListboxHeader.find("void SetItemColors(ColorRGBA SelectedActive, ColorRGBA SelectedInactive, ColorRGBA Hovered)"), std::string::npos);
	EXPECT_NE(Listbox.find("m_SelectedItemActiveColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);"), std::string::npos);
	EXPECT_NE(Listbox.find("Ui()->ScaleBackgroundAlpha(m_Active ? m_SelectedItemActiveColor : m_SelectedItemInactiveColor)"), std::string::npos);
	EXPECT_NE(Theme.find("s_ListBox.SetItemColors(ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_SELECTED, ui_token::color::LIST_ITEM_HOVER);"), std::string::npos);
	const size_t LanguageSelectedCondition = Language.find("if(Selected)");
	const size_t LanguageSelected = Language.find("DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_SELECTED)");
	const size_t LanguageHoveredCondition = Language.find("if(Ui()->HotItem() == pRowId)");
	const size_t LanguageHovered = Language.find("DrawRoundedSurface(Ui(), ItemRect, Ui()->ScaleBackgroundAlpha(ui_token::color::LIST_ITEM_HOVER)");
	ASSERT_NE(LanguageSelectedCondition, std::string::npos);
	ASSERT_NE(LanguageSelected, std::string::npos);
	ASSERT_NE(LanguageHoveredCondition, std::string::npos);
	ASSERT_NE(LanguageHovered, std::string::npos);
	EXPECT_LT(LanguageSelectedCondition, LanguageSelected);
	EXPECT_LT(LanguageHoveredCondition, LanguageHovered);
	EXPECT_LT(LanguageSelected, LanguageHovered);
}

TEST(QmNewUiMenuSettingsColorsContract, ColorPickerUsesIndependentPointerCapture)
{
	const std::string Source = ReadTextFile("src/game/client/ui.cpp");
	const std::string Picker = FunctionBody(Source, "EEditState CUi::DoPickerLogic(");
	ASSERT_FALSE(Picker.empty());

	EXPECT_NE(Picker.find("const bool Inside = MouseHovered(pRect);"), std::string::npos);
	EXPECT_NE(Picker.find("if(Inside && MouseButtonClicked(0))"), std::string::npos);
	EXPECT_NE(Picker.find("if(!CheckActiveItem(pId))"), std::string::npos);
	EXPECT_NE(Picker.find("if(!MouseButton(0))"), std::string::npos);
	EXPECT_EQ(Picker.find("m_pLastEditingItem"), std::string::npos);
}

TEST(QmNewUiMenuSettingsColorsContract, RenderPopupMenusAlwaysPairsPopupInputDepth)
{
	// m_PopupInputDepth 只能由 RenderPopupMenus 配对递减复位。点击弹窗外关闭的路径
	// 若提前 continue 会泄漏深度，使所有弹窗的底层输入屏蔽永久失效：弹窗下面的
	// 设置页重新响应鼠标并抢占颜色选择器的拖拽捕获，表现为拖拽断断续续。该约束
	// 需要真实鼠标与弹窗状态才能在运行时观察，当前测试环境无法构造，因此以源码
	// 合同固定“递增与递减之间不得提前退出”。
	const std::string Source = ReadTextFile("src/game/client/ui_popups.cpp");
	const std::string Render = FunctionBody(Source, "void CUi::RenderPopupMenus()");
	ASSERT_FALSE(Render.empty());

	const size_t Increment = Render.find("++m_PopupInputDepth;");
	const size_t Decrement = Render.find("--m_PopupInputDepth;");
	ASSERT_NE(Increment, std::string::npos);
	ASSERT_NE(Decrement, std::string::npos);
	ASSERT_LT(Increment, Decrement);
	EXPECT_EQ(Render.substr(Increment, Decrement - Increment).find("continue;"), std::string::npos);
	EXPECT_NE(Render.find("bool CloseBeforeRender = false;"), std::string::npos);
	EXPECT_NE(Render.find("if(CloseBeforeRender)"), std::string::npos);
}
