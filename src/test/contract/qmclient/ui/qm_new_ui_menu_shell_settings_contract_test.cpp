// QmNewUi 菜单源码合同：设置外壳与卡片页面域。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
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
#include <test/support/qmclient_source_contract_test.h>
#include <test/test.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuShellSettingsContract, SettingsShellAndOuterScrollbarUseStableContracts)
{
	const std::string ShellSource = ReadTextFile("src/game/client/QmUi/SettingsPageLayout.h");
	const std::string TokenSource = ReadTextFile("src/game/client/QmUi/UiTokens.h");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string ScrollSource = ReadTextFile("src/game/client/QmUi/QmScroll.cpp");
	const std::string AssetsSource = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");
	EXPECT_NE(TokenSource.find("MAX_CONTENT_WIDTH = 1000.0f"), std::string::npos);
	EXPECT_NE(ShellSource.find("ResolveSettingsShellLayout"), std::string::npos);
	EXPECT_NE(ShellSource.find("Frame.m_ShellRect.VSplitRight"), std::string::npos);
	EXPECT_NE(ShellSource.find("Frame.m_ContentRect.Margin(PanelMargin"), std::string::npos);
	EXPECT_NE(SettingsSource.find("ResolveSettingsShellLayout(MainView, NeedRestart ? 30.0f : 0.0f)"), std::string::npos);
	EXPECT_NE(ScrollSource.find("SETTINGS_OUTER"), std::string::npos);
	EXPECT_NE(ScrollSource.find("QmScrollContainerStyleForSize(EQmScrollSize::MEDIUM, 1.0f)"), std::string::npos);
	EXPECT_NE(AssetsSource.find("s_ListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);"), std::string::npos);
	EXPECT_NE(AssetsSource.find("s_WorkshopAssetsListBox.SetScrollProfile(EQmScrollProfile::SETTINGS_OUTER);"), std::string::npos);
	EXPECT_NE(AssetsSource.find("StableCustomList.w / (Margin + TextureWidth)"), std::string::npos);
}

TEST(QmNewUiMenuShellSettingsContract, QmLocalizationEnglishOverlayUsesExplicitEnglishFile)
{
	const std::string Source = ReadTextFile("src/game/client/gameclient.cpp");

	EXPECT_EQ(Source.find("str_format(aBuf, sizeof(aBuf), \"qmclient/%s\", g_Config.m_ClLanguagefile);"), std::string::npos);
	EXPECT_EQ(Source.find("static void LoadQmClientLanguageOverlay("), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = g_Config.m_ClLanguagefile[0] != '\\0' ? g_Config.m_ClLanguagefile : \"english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = pLanguageFile[0] != '\\0' ? pLanguageFile : \"english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("if(str_comp(pLanguageFile, \"languages/simplified_chinese.txt\") == 0)"), std::string::npos);
	EXPECT_EQ(Source.find("const char *pQmLanguageFile = pLanguageFile[0] != '\\0' ? pLanguageFile : \"languages/english.txt\";"), std::string::npos);
	EXPECT_EQ(Source.find("str_format(aBuf, sizeof(aBuf), \"qmclient/%s\", pQmLanguageFile);"), std::string::npos);
	EXPECT_EQ(Source.find("LoadQmClientLanguageOverlay(g_Localization, g_Config.m_ClLanguagefile, Storage(), Console());"), std::string::npos);
	EXPECT_NE(Source.find("g_Localization.Load(g_Config.m_ClLanguagefile, Storage(), Console());"), std::string::npos);
}

TEST(QmNewUiMenuShellSettingsContract, QmClientTabLabelsDoNotCacheLocalizedPointers)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_EQ(Source.find("static const char *s_apQmTabNames"), std::string::npos);
	EXPECT_EQ(Source.find("s_aQmLanguageFile"), std::string::npos);
	EXPECT_NE(Source.find("const char *apQmTabNames[NUMBER_OF_QMCLIENT_SETTINGS_TABS] = {};"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_VISUAL] = Localize(\"Visuals\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_FUNCTION] = Localize(\"Functions\");"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_HUD] = Localize(\"HUD\");"), std::string::npos);
	// 贡献者页签并入顶层贡献者页：页签栏不再分配 Contributors 标签，枚举值仅作持久化占位。
	EXPECT_EQ(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_CONTRIBUTORS]"), std::string::npos);
	EXPECT_NE(Source.find("apQmTabNames[QMCLIENT_SETTINGS_TAB_CONFIG] = Localize(\"Config\");"), std::string::npos);
}

TEST(QmNewUiMenuShellSettingsContract, AssetsPreviewUsesInnerFrameRectForPreviewImage)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_settings_assets.cpp");

	EXPECT_NE(Source.find("auto DrawPreviewFrame = [&](const CUIRect &TextureRect) -> CUIRect {"), std::string::npos);
	EXPECT_NE(Source.find("PreviewFrame.Margin(3.0f, &PreviewFrame);"), std::string::npos);
	EXPECT_NE(Source.find("return PreviewFrame;"), std::string::npos);
	EXPECT_NE(Source.find("auto ComputeAssetPreviewContentSize = [&](bool WorkshopCard)"), std::string::npos);
	EXPECT_NE(Source.find("CUIRect PreviewFrameRect = DrawPreviewFrame(Shell.m_TextureRect);"), std::string::npos);
	EXPECT_NE(Source.find("const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(WorkshopCard);"), std::string::npos);
	EXPECT_NE(Source.find("const auto [PreviewContentWidth, PreviewContentHeight] = ComputeAssetPreviewContentSize(true);"), std::string::npos);
	EXPECT_NE(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(PreviewFrameRect, PreviewContentWidth, PreviewContentHeight);"), std::string::npos);
	EXPECT_EQ(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(HeaderLayout.m_TextureRect, TextureWidth, TextureHeight);"), std::string::npos);
	EXPECT_EQ(Source.find("const CUIRect PreviewRect = ComputePreviewDrawRect(HeaderLayout.m_TextureRect, TextureWidth, TextureWidth);"), std::string::npos);
}
