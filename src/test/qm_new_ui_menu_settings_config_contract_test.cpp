// QmNewUi 菜单源码合同：配置默认值与迁移域：Qm 默认关闭策略、显式遗留值保留、配置帮助文本本地化。
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

#include <regex>
#include <sstream>
#include <string>

TEST(QmNewUiMenuSettingsConfigContract, ConfigPageLocalizesVariableHelpText)
{
	const std::string ConfigHeader = ReadTextFile("src/engine/shared/config.h");
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config.cpp");
	const std::string TClientMenusSource = ReadTextFile("src/game/client/components/tclient/menus_tclient.cpp");

	EXPECT_NE(ConfigHeader.find("const char *m_pHelpLocalizeKey;"), std::string::npos);
	EXPECT_NE(ConfigSource.find("SConfigVariable::VAR_INT, Flags, m_ConfigHeap.StoreString(aHelp), pDesc"), std::string::npos);
	EXPECT_NE(ConfigSource.find("SConfigVariable::VAR_COLOR, Flags, m_ConfigHeap.StoreString(aHelp), Desc"), std::string::npos);
	EXPECT_NE(ConfigSource.find("SConfigVariable::VAR_STRING, Flags, m_ConfigHeap.StoreString(aHelp), Desc"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("BuildLocalizedConfigHelpText"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("pVar->m_pHelpLocalizeKey ? pVar->m_pHelpLocalizeKey"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("Localize(pHelpKey)"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("s_CachedConfigLanguageHash"), std::string::npos);
	EXPECT_NE(TClientMenusSource.find("str_quickhash(g_Config.m_ClLanguagefile)"), std::string::npos);
	EXPECT_EQ(TClientMenusSource.find("Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : \"\""), std::string::npos);
}
