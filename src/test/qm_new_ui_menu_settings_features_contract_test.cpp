// QmNewUi 菜单源码合同：Qm 功能开关域：动态岛行预布局、武器轨迹/动画、皮肤切换过渡、进程优先级与 IME、表情阴影。
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

TEST(QmNewUiMenuSettingsFeaturesContract, DynamicIslandSettingsOmitsEdgeMarginControl)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string Body = FunctionBody(Source, "void CMenus::RenderQmHudDynamicIslandContent(");
	ASSERT_FALSE(Body.empty());

	EXPECT_EQ(Body.find("QmHudIslandEdgeMargin"), std::string::npos);
	EXPECT_EQ(Body.find("Localize(\"Edge margin\")"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, DynamicIslandEdgeMarginIsOnlyAnIgnoredLegacyCommand)
{
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string GameClient = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string Callback = FunctionBody(GameClient, "void ConDiscardLegacyHudIslandEdgeMargin(");
	const std::string OnConsoleInit = FunctionBody(GameClient, "void CGameClient::OnConsoleInit()");

	EXPECT_EQ(Config.find("qm_hud_island_edge_margin"), std::string::npos);
	ASSERT_FALSE(Callback.empty());
	EXPECT_EQ(Callback.find("g_Config"), std::string::npos);
	EXPECT_NE(OnConsoleInit.find("pConsole->Register(\"qm_hud_island_edge_margin\", \"?i[value]\", CFGFLAG_CLIENT, ConDiscardLegacyHudIslandEdgeMargin, nullptr"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, DynamicIslandPreLayoutConsumesTheSameConditionalRows)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const size_t FactoryPos = Source.find("const auto BuildHudPreLayoutInput");
	ASSERT_NE(FactoryPos, std::string::npos);
	const std::string PreLayoutSource = Source.substr(FactoryPos);
	const size_t DynamicIslandPos = PreLayoutSource.find("case EQmModuleId::DynamicIsland:");
	const size_t PlayerStatsPos = PreLayoutSource.find("case EQmModuleId::PlayerStats:", DynamicIslandPos);
	ASSERT_NE(DynamicIslandPos, std::string::npos);
	ASSERT_NE(PlayerStatsPos, std::string::npos);
	const std::string DynamicIsland = PreLayoutSource.substr(DynamicIslandPos, PlayerStatsPos - DynamicIslandPos);

	EXPECT_NE(DynamicIsland.find("g_Config.m_QmHudIslandUseOriginalStyle"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("g_Config.m_QmHudIslandShowTeam"), std::string::npos);
	EXPECT_EQ(DynamicIsland.find("ConsumeQmHudRow(Content); // edge margin"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("ResolveSettingsColorRowLayout(Content, Metrics, false)"), std::string::npos);
	EXPECT_NE(DynamicIsland.find("if(!g_Config.m_QmHudIslandUseOriginalStyle)"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, WeaponAnimationAdvancedControlsAreConfigurable)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string RegistrySource = ReadTextFile("src/game/client/QmUi/QmCardRegistry.cpp");

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimDurationMs, qm_weapon_switch_anim_duration_ms, 300"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimDistance, qm_weapon_switch_anim_distance, 40"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimRotation, qm_weapon_switch_anim_rotation, 360"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponSwitchAnimEasing, qm_weapon_switch_anim_easing"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponReloadAnim, qm_weapon_reload_anim"), std::string::npos);
	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmWeaponReloadAnimProbability, qm_weapon_reload_anim_probability"), std::string::npos);

	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimDurationMs"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimDistance"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimRotation"), std::string::npos);
	EXPECT_NE(PlayersSource.find("g_Config.m_QmWeaponSwitchAnimEasing"), std::string::npos);

	const std::string WeaponAnimationContent = FunctionBody(MenusSource, "void CMenus::RenderQmVisualWeaponAnimationContent(");
	const size_t SwitchToggle = WeaponAnimationContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponSwitchAnim");
	const size_t ReloadToggle = WeaponAnimationContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim");
	const size_t ReloadProbability = WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-reload-animation-probability\", \"Weapon reload animation probability\"");
	const size_t SharedControls = WeaponAnimationContent.find("if(!g_Config.m_QmWeaponSwitchAnim && !g_Config.m_QmWeaponReloadAnim)");
	const size_t SwitchControls = WeaponAnimationContent.find("if(!g_Config.m_QmWeaponSwitchAnim)", SharedControls);
	ASSERT_NE(SwitchToggle, std::string::npos);
	ASSERT_NE(ReloadToggle, std::string::npos);
	ASSERT_NE(ReloadProbability, std::string::npos);
	ASSERT_NE(SharedControls, std::string::npos);
	ASSERT_NE(SwitchControls, std::string::npos);
	EXPECT_LT(SwitchToggle, ReloadToggle);
	EXPECT_LT(ReloadToggle, ReloadProbability);
	EXPECT_LT(ReloadProbability, SharedControls);
	EXPECT_LT(SharedControls, SwitchControls);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-duration\", \"Weapon switch duration\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-distance\", \"Weapon switch distance\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("RenderValue(\"qmclient-weapon-switch-rotation\", \"Weapon switch rotation\"", SwitchControls), std::string::npos);
	EXPECT_NE(WeaponAnimationContent.find("Localize(\"Weapon switch easing\")", SwitchControls), std::string::npos);

	const std::string VisualDeck = FunctionBody(MenusSource, "void CMenus::RenderSettingsQmClientVisualDeck(");
	EXPECT_NE(VisualDeck.find("ResolveQmVisualWeaponAnimationHeight(Metrics, g_Config.m_QmWeaponSwitchAnim != 0, g_Config.m_QmWeaponReloadAnim != 0)"), std::string::npos);
	EXPECT_NE(VisualDeck.find("(g_Config.m_QmWeaponReloadAnim ? 2u : 0u)"), std::string::npos);
	EXPECT_NE(VisualDeck.find("HandleQmHudCheckboxInput(Content, LineHeight, LineSpacing, &g_Config.m_QmWeaponReloadAnim, &g_Config.m_QmWeaponReloadAnim)"), std::string::npos);
	EXPECT_NE(RegistrySource.find("装填动画 zhuangtian donghua reload animation"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, ProcessPrioritySettingIsRemovedAndImeRemainsVisible)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_EQ(ConfigSource.find("QmProcessHighPriority"), std::string::npos);
	EXPECT_EQ(ClientSource.find("ApplyProcessPriorityConfig"), std::string::npos);
	EXPECT_EQ(ClientSource.find("qm_process_high_priority"), std::string::npos);
	const std::string MiniFeaturesBody = FunctionBody(MenusSource, "void CMenus::RenderQmFunctionMiniFeaturesContent(");
	ASSERT_FALSE(MiniFeaturesBody.empty());
	EXPECT_EQ(MiniFeaturesBody.find("QmProcessHighPriority"), std::string::npos);
	EXPECT_NE(MiniFeaturesBody.find("&g_Config.m_QmImeAutoManage"), std::string::npos);
	EXPECT_NE(MiniFeaturesBody.find("&g_Config.m_QmNewIme"), std::string::npos);
}

TEST(QmNewUiMenuSettingsFeaturesContract, EmoticonShadowHasConfigRenderPassAndVisualToggle)
{
	const std::string ConfigSource = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string PlayersSource = ReadTextFile("src/game/client/components/players.cpp");
	const std::string EmoticonSource = ReadTextFile("src/game/client/components/emoticon.cpp");
	const std::string RenderPlayerBody = FunctionBody(PlayersSource, "void CPlayers::RenderPlayer(");
	const std::string EmoticonRenderBody = FunctionBody(EmoticonSource, "void CEmoticon::OnRender()");
	const std::string EmoticonItemsBody = BlockBodyAfter(EmoticonRenderBody, "for(int Emote = 0; Emote < NUM_EMOTICONS; Emote++)");
	const std::string MenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const auto CountOccurrences = [](const std::string &Text, const char *pNeedle) {
		int Count = 0;
		size_t Pos = 0;
		while((Pos = Text.find(pNeedle, Pos)) != std::string::npos)
		{
			++Count;
			Pos += str_length(pNeedle);
		}
		return Count;
	};

	EXPECT_NE(ConfigSource.find("MACRO_CONFIG_INT(QmEmoticonShadow, qm_emoticon_shadow, 0, 0, 1"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOpacity"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetX"), std::string::npos);
	EXPECT_EQ(CountOccurrences(RenderPlayerBody, "if(g_Config.m_QmEmoticonShadow)"), 3);
	EXPECT_EQ(CountOccurrences(RenderPlayerBody, "Graphics()->SetColor(0.0f, 0.0f, 0.0f"), 3);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetX * h"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("EmoticonShadowOffsetY * h"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);\n\t\tGraphics()->RenderQuadContainerAsSprite"), std::string::npos);
	EXPECT_NE(RenderPlayerBody.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, a * Alpha);\n\t\t\tGraphics()->RenderQuadContainerAsSprite"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("EmoticonSelectorShadowOpacity"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("if(g_Config.m_QmEmoticonShadow)"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("Graphics()->SetColor(0.0f, 0.0f, 0.0f, EmoticonSelectorShadowOpacity);"), std::string::npos);
	EXPECT_NE(EmoticonRenderBody.find("ScreenCenter.x + Nudge.x + EmoticonSelectorShadowOffsetX"), std::string::npos);
	EXPECT_EQ(CountOccurrences(EmoticonItemsBody, "Graphics()->QuadsBegin();"), 2);
	const size_t ShadowBranch = EmoticonItemsBody.find("if(g_Config.m_QmEmoticonShadow)");
	ASSERT_NE(ShadowBranch, std::string::npos);
	const size_t ShadowClear = EmoticonItemsBody.find("Graphics()->TextureClear();", ShadowBranch);
	const size_t ShadowBegin = EmoticonItemsBody.find("Graphics()->QuadsBegin();", ShadowBranch);
	ASSERT_NE(ShadowClear, std::string::npos);
	ASSERT_NE(ShadowBegin, std::string::npos);
	EXPECT_LT(ShadowClear, ShadowBegin);
	// 皮肤卡的内容函数已迁入全局卡片目录（N3）：改在目录文件里定位函数体。
	// 「表情阴影」开关仍在皮肤外观卡内（QmCardCatalogSkin.cpp），未因迁移丢失。
	const std::string SkinCardSource = ReadTextFile("src/game/client/QmUi/cards/QmCardCatalogSkin.cpp");
	const std::string SkinAppearanceContent = FunctionBody(SkinCardSource, "void CMenus::RenderQmVisualSkinAppearanceContent(");
	const std::string SkinTransitionContent = FunctionBody(SkinCardSource, "void CMenus::RenderQmVisualSkinTransitionContent(");
	EXPECT_NE(SkinAppearanceContent.find("RenderQmVisualCheckbox(Content, LineHeight, LineSpacing, &g_Config.m_QmEmoticonShadow"), std::string::npos);
	EXPECT_EQ(SkinTransitionContent.find("g_Config.m_QmEmoticonShadow"), std::string::npos);
	EXPECT_NE(SkinCardSource.find("Localize(\"Emoticon shadow\")"), std::string::npos);
}
