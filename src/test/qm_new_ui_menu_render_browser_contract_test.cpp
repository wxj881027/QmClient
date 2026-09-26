// QmNewUi 菜单源码合同：render 浏览器、地图选择与索引钳制。运行时行为保留在 qm_new_ui_menu_branch_test.cpp.
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

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuRenderBrowserContract, TClientProfilesAndStatusBarClampUiIndices)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");
	const std::string RenderSettingsTClientProfiles = FunctionBody(Source, "void CMenus::RenderSettingsTClientProfiles(CUIRect MainView, bool PrewarmOnly)");
	const std::string RenderSettingsTClientStatusBar = FunctionBody(Source, "void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView, bool PrewarmOnly)");

	EXPECT_NE(RenderSettingsTClientProfiles.find("Profile.m_FeetColor >= 0"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientProfiles.find("ProfilesPerRow = maximum(1"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("StatusItemTypeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_TypeSelectedOld < StatusItemTypeCount"), std::string::npos);
	EXPECT_NE(RenderSettingsTClientStatusBar.find("s_SelectedItem < (int)GameClient()->m_StatusBar.m_StatusBarItems.size()"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, SettingsListSelectionsClampBeforeIndexing)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsPlayer = FunctionBody(Source, "void CMenus::RenderSettingsPlayer(CUIRect MainView)");
	const std::string RenderSettingsGraphics = FunctionBody(Source, "void CMenus::RenderSettingsGraphics(CUIRect MainView)");
	const std::string PopupMapPicker = FunctionBody(Source, "CUi::EPopupMenuFunctionResult CMenus::PopupMapPicker(void *pContext, CUIRect View, bool Active)");

	EXPECT_NE(RenderSettingsPlayer.find("NewSelected >= 0 && NewSelected < (int)s_vFilteredFlags.size()"), std::string::npos);
	EXPECT_NE(RenderSettingsGraphics.find("NewSelected >= 0 && NewSelected < s_NumNodes"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("const int ItemIndex = MapIndex++;"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("ItemIndex == pPopupContext->m_Selection"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("NewSelected >= 0 && NewSelected < (int)pPopupContext->m_vMaps.size()"), std::string::npos);
	EXPECT_NE(PopupMapPicker.find("pPopupContext->m_Selection >= 0"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, BackgroundMapPickerUsesMapsRootAndSupportedFiles)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string RenderSettingsDDNet = FunctionBody(Source, "void CMenus::RenderSettingsDDNet(CUIRect MainView)");
	const std::string MapListPopulate = FunctionBody(Source, "void CMenus::CPopupMapPickerContext::MapListPopulate()");
	const std::string MapListFetchCallback = FunctionBody(Source, "int CMenus::CPopupMapPickerContext::MapListFetchCallback");

	EXPECT_NE(RenderSettingsDDNet.find("str_copy(s_PopupMapPickerContext.m_aRootPath, \"maps\""), std::string::npos);
	EXPECT_NE(MapListPopulate.find("ListRoot(m_aRootPath[0] != '\\0' ? m_aRootPath : \"maps\", m_aValuePrefix);"), std::string::npos);
	EXPECT_EQ(MapListPopulate.find("m_aFallbackRootPath"), std::string::npos);
	EXPECT_EQ(MapListPopulate.find("m_aFallbackValuePrefix"), std::string::npos);
	EXPECT_NE(MapListFetchCallback.find("FindBackgroundFileExtension(pInfo->m_pName)"), std::string::npos);
	EXPECT_EQ(MapListFetchCallback.find("str_endswith(pInfo->m_pName, \".map\")"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, EditorSaveFileDialogKeepsFilenameInputInControl)
{
	const std::string Source = ReadTextFile("src/game/editor/file_browser.cpp");
	const std::string OnRender = FunctionBody(Source, "void CFileBrowser::OnRender(CUIRect _)");

	EXPECT_NE(OnRender.find("m_ListBox.SetActive(!Ui()->IsPopupOpen() && (!m_SaveAction || !m_FilenameInput.IsActive()))"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool ListChoseItem = m_ListBox.WasItemSelected() || m_ListBox.WasItemActivated();"), std::string::npos);
	EXPECT_NE(OnRender.find("const bool SyncFilenameInput = !m_SaveAction || (ListChoseItem && m_SelectedFileIndex >= 0);"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, CallVoteSearchSupportsIndependentExclusion)
{
	const std::string MenusHeader = ReadTextFile("src/game/client/components/menus.h");
	const std::string IngameMenus = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string ServerList = FunctionBody(IngameMenus, "bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)");
	const std::string PlayerList = FunctionBody(IngameMenus, "bool CMenus::RenderServerControlKick(CUIRect MainView, bool FilterSpectators, bool UpdateScroll)");
	const std::string RenderControl = FunctionBody(IngameMenus, "void CMenus::RenderServerControl(CUIRect MainView)");

	ASSERT_FALSE(ServerList.empty());
	ASSERT_FALSE(PlayerList.empty());
	ASSERT_FALSE(RenderControl.empty());
	EXPECT_NE(MenusHeader.find("CLineInputBuffered<64> m_ExcludeInput;"), std::string::npos);
	EXPECT_NE(ServerList.find("QmTextMatchesIncludeExcludeFilter(pOption->m_aDescription, m_FilterInput.GetString(), m_ExcludeInput.GetString())"), std::string::npos);
	EXPECT_NE(PlayerList.find("QmTextMatchesIncludeExcludeFilter(GameClient()->m_aClients[Index].m_aName, m_FilterInput.GetString(), m_ExcludeInput.GetString())"), std::string::npos);
	EXPECT_NE(RenderControl.find("ingame_callvote_exclude"), std::string::npos);
	EXPECT_NE(RenderControl.find("CallvoteExcludeOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;"), std::string::npos);
	EXPECT_NE(RenderControl.find("CallvoteExcludeOptions.m_pPlaceholder = Localize(\"Exclude\");"), std::string::npos);
	EXPECT_NE(RenderControl.find("const float MapSortWidth = HasMapSort ? 140.0f : 0.0f;"), std::string::npos);
	EXPECT_NE(RenderControl.find("const float FilterWidth = std::min(220.0f, std::max(1.0f, (Bottom.w - 5.0f - MapSortWidth - MapSortGap) * 0.5f));"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, BrowserSearchUsesSharedIconAndExcludeKeepsItsOwnIconAndTooltipRect)
{
	const std::string Browser = FunctionBody(ReadTextFile("src/game/client/components/menus_browser.cpp"), "void CMenus::RenderServerbrowserStatusBox(CUIRect StatusBox, bool WasListboxItemActivated)");

	ASSERT_FALSE(Browser.empty());
	EXPECT_NE(Browser.find("ui_widget::InputField(ServerBrowserSearchCtx, &s_FilterInput, QuickSearch, SearchOptions)"), std::string::npos);
	// 两个标签都不得带图标：图标只出现在输入框内部。
	EXPECT_EQ(Browser.find("Ui()->DoLabel(&QuickSearch, FONT_ICON_MAGNIFYING_GLASS"), std::string::npos);
	EXPECT_EQ(Browser.find("DoLabel_QmIcon(&QuickExclude"), std::string::npos);
	// 排除框保留自己的图标语义（BAN，而非放大镜）与「排除」占位符。
	EXPECT_NE(Browser.find("ExcludeOptions.m_pLeadingIcon = FONT_ICON_BAN"), std::string::npos);
	EXPECT_NE(Browser.find("ExcludeOptions.m_LeadingQmIcon = static_cast<int>(EQmIcon::BAN)"), std::string::npos);
	EXPECT_NE(Browser.find("ExcludeOptions.m_pPlaceholder = Localize(\"Exclude\")"), std::string::npos);
	EXPECT_NE(Browser.find("DoToolTip(&s_ExcludeInput, &QuickExclude"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, IngameFavoriteMapsUsesSharedBookmarkIcon)
{
	const std::string Ingame = FunctionBody(ReadTextFile("src/game/client/components/menus_ingame.cpp"), "void CMenus::RenderInGameNetwork(CUIRect MainView)");

	ASSERT_FALSE(Ingame.empty());
	EXPECT_NE(Ingame.find("DoMenuTabV2(&s_FavoriteMapsButton, \"\", g_Config.m_UiPage == PAGE_FAVORITE_MAPS"), std::string::npos);
	EXPECT_NE(Ingame.find("QmIconManager()->RenderIcon(EQmIcon::BOOKMARK"), std::string::npos);
	EXPECT_NE(Ingame.find("FONT_ICON_BOOKMARK"), std::string::npos);
	EXPECT_NE(Ingame.find("TextRender()->TextColor(OldTextColor)"), std::string::npos);
	EXPECT_EQ(Ingame.find("\xF0\x9F\x94\x96"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, GraphicsCurrentModeLabelSanitizesScaleAndAspectRatio)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings.cpp");

	EXPECT_NE(Source.find("const float HiDPIScale = std::isfinite(RawHiDPIScale) && RawHiDPIScale > 0.0f ? RawHiDPIScale : 1.0f;"), std::string::npos);
	EXPECT_NE(Source.find("const int AspectGcd = G > 0 ? G : 1;"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxScreenWidth / AspectGcd"), std::string::npos);
	EXPECT_NE(Source.find("g_Config.m_GfxScreenHeight / AspectGcd"), std::string::npos);
}

TEST(QmNewUiMenuRenderBrowserContract, TeePresetListUsesTheSameRowSpacingAsItsMeasuredViewport)
{
	const std::string Settings = FunctionBody(ReadTextFile("src/game/client/components/menus_settings.cpp"), "void CMenus::RenderSettingsTee(CUIRect MainView)");

	ASSERT_FALSE(Settings.empty());
	EXPECT_NE(Settings.find("const float PresetRowSpacing = TeeMetrics.m_LineSpacing * 0.5f;"), std::string::npos);
	EXPECT_NE(Settings.find("s_PresetListBox.DoAutoSpacing(PresetRowSpacing);"), std::string::npos);
	EXPECT_NE(Settings.find("s_PresetListBox.DoNextItem(&s_vPresetItemIds[i], ActivePresetIndex == (int)i, PresetRowSpacing)"), std::string::npos);
}
