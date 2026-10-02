// QmNewUi 菜单源码合同：新版 UI 的配置迁移域与配置帮助文本本地化。
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
#include <test/support/qmclient_source_contract_test.h>
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

TEST(QmNewUiMenuBranches, LegacyConfigMigrationKeepsExplicitValues)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config.cpp");
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string DomainSource = ReadTextFile("src/engine/shared/config_domains.h");
	const std::string IncludeSource = ReadTextFile("src/engine/shared/config_includes.h");

	EXPECT_NE(IncludeSource.find("SET_CONFIG_DOMAIN(ConfigDomain::QMCLIENT)\n#include \"config_variables_qmclient.h\""), std::string::npos);
	EXPECT_NE(DomainSource.find("CONFIG_DOMAIN(QMCLIENT, \"qmclient/settings.cfg\", nullptr, nullptr, true)"), std::string::npos);
	EXPECT_NE(ClientSource.find("pConfigManager->Init();"), std::string::npos);
	EXPECT_NE(ClientSource.find("if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))"), std::string::npos);
	EXPECT_LT(ClientSource.find("pConfigManager->Init();"), ClientSource.find("if(!pConsole->ExecuteFile(pConfigPath, IConsole::CLIENT_ID_UNSPECIFIED))"));
	EXPECT_NE(ConfigSource.find("pVariable->m_ConfigDomain == ConfigDomain && (pVariable->m_Flags & CFGFLAG_SAVE) != 0 && !pVariable->IsDefault()"), std::string::npos);
	EXPECT_NE(ConfigSource.find("std::vector<char> vLineBuf(pVariable->MaxSerializedSize());"), std::string::npos);
	EXPECT_NE(ConfigSource.find("pVariable->Serialize(vLineBuf.data(), vLineBuf.size());"), std::string::npos);
	EXPECT_NE(ConfigSource.find("WriteLine(vLineBuf.data(), ConfigDomain);"), std::string::npos);
	EXPECT_EQ(ConfigSource.find("Reset(\"qm_"), std::string::npos);
}
