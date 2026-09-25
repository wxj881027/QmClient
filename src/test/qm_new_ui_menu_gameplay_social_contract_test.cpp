// QmNewUi 菜单源码合同：gameplay 好友、服务器名与重命名。运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
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

TEST(QmNewUiMenuGameplaySocialContract, ProtectedFriendCategoriesCannotBeRenamedOrDeleted)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const size_t ProtectedFn = Source.find("static bool IsProtectedFriendsCategory");
	ASSERT_NE(ProtectedFn, std::string::npos);
	const size_t ProtectedFnEnd = Source.find("static const char *LocalizeFriendsCategory", ProtectedFn);
	ASSERT_NE(ProtectedFnEnd, std::string::npos);
	const std::string ProtectedBody = Source.substr(ProtectedFn, ProtectedFnEnd - ProtectedFn);

	EXPECT_NE(ProtectedBody.find("IFriends::DEFAULT_CATEGORY"), std::string::npos);
	EXPECT_NE(ProtectedBody.find("IsClanMembersCategory(pCategory)"), std::string::npos);
	EXPECT_NE(ProtectedBody.find("IsOfflineFriendsCategory(pCategory)"), std::string::npos);

	const size_t Popup = Source.find("CUi::EPopupMenuFunctionResult CMenus::PopupFriendsCategory");
	ASSERT_NE(Popup, std::string::npos);
	const std::string PopupBody = Source.substr(Popup);
	EXPECT_NE(PopupBody.find("const bool IsProtectedCategory = IsProtectedFriendsCategory(pCategory);"), std::string::npos);
	EXPECT_NE(PopupBody.find("Localize(\"Rename\"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory"), std::string::npos);
	EXPECT_NE(PopupBody.find("Localize(\"Delete category\"), &Button, FontSize, TEXTALIGN_MC, 0.0f, false, !IsProtectedCategory"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendAddPopupExposesCreateCategoryAction)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Header.find("m_FriendsAddCategoryCreateButton"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Create category\")"), std::string::npos);
	EXPECT_NE(Source.find("m_FriendsCategoryPopupContext.m_Mode = CFriendsCategoryPopupContext::MODE_ADD"), std::string::npos);
	EXPECT_NE(Source.find("Ui()->DoPopupMenu(&m_FriendsCategoryPopupContext"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendCategoryHeaderActionExcludesManageButton)
{
	const CUIRect Header{10.0f, 20.0f, 180.0f, 24.0f};
	CUIRect HeaderAction;
	CUIRect ManageButton;

	CMenus::SplitFriendsCategoryHeaderRects(Header, &HeaderAction, &ManageButton);

	EXPECT_FLOAT_EQ(HeaderAction.x, Header.x);
	EXPECT_FLOAT_EQ(HeaderAction.y, Header.y);
	EXPECT_FLOAT_EQ(HeaderAction.w, Header.w - Header.h);
	EXPECT_FLOAT_EQ(HeaderAction.h, Header.h);
	EXPECT_FLOAT_EQ(ManageButton.x, Header.x + Header.w - Header.h + 2.0f);
	EXPECT_FLOAT_EQ(ManageButton.y, Header.y + 2.0f);
	EXPECT_FLOAT_EQ(ManageButton.w, Header.h - 4.0f);
	EXPECT_FLOAT_EQ(ManageButton.h, Header.h - 4.0f);
	EXPECT_LE(HeaderAction.x + HeaderAction.w, ManageButton.x);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendCategoryEditPopupHasRoomForInputAndActions)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	constexpr float RequiredHeight = 5.0f * 2.0f + 12.0f + 3.0f + 18.0f + 6.0f + 20.0f;

	EXPECT_FLOAT_EQ(CMenus::FriendsCategoryEditPopupHeight(), RequiredHeight);
	EXPECT_GT(CMenus::FriendsCategoryActionsPopupHeight(), 60.0f);
	EXPECT_NE(Source.find("CMenus::FriendsCategoryEditPopupHeight()"), std::string::npos);
	EXPECT_NE(Source.find("CMenus::SecondaryPanelRect("), std::string::npos);
	EXPECT_EQ(Source.find("250.0f, 62.0f"), std::string::npos);
	EXPECT_EQ(Source.find("250.0f, 110.0f"), std::string::npos);
	EXPECT_EQ(Source.find("280.0f, CMenus::FriendsCategoryEditPopupHeight()"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, SecondaryPanelRectClampsNearScreenEdges)
{
	const CUIRect Screen{0.0f, 0.0f, 800.0f, 600.0f};
	const CUIRect Panel = CMenus::SecondaryPanelRect(780.0f, 590.0f, 300.0f, 140.0f, Screen);

	EXPECT_FLOAT_EQ(Panel.w, 300.0f);
	EXPECT_FLOAT_EQ(Panel.h, 140.0f);
	EXPECT_LE(Panel.x + Panel.w, 792.0f);
	EXPECT_LE(Panel.y + Panel.h, 592.0f);
	EXPECT_GE(Panel.x, 8.0f);
	EXPECT_GE(Panel.y, 8.0f);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendAutoFollowDelaysAndStopsAfterTwoJumps)
{
	CMenus::SFriendAutoFollowState State;
	char aConnect[NETADDR_MAXSTRSIZE] = "";

	CMenus::StartFriendAutoFollow(State, "Alice", "Clan", "127.0.0.1:8303");
	EXPECT_TRUE(State.m_Active);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8303", 10.0f, 3, 2, aConnect, sizeof(aConnect)));

	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 11.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(State.m_HasPendingAddress);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 13.9f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 14.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_STREQ(aConnect, "127.0.0.1:8304");
	EXPECT_TRUE(State.m_Active);
	EXPECT_EQ(State.m_JumpCount, 1);

	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8305", 20.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8305", 23.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_STREQ(aConnect, "127.0.0.1:8305");
	EXPECT_FALSE(State.m_Active);
	EXPECT_EQ(State.m_JumpCount, 2);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendAutoFollowCancelsWhenTargetGoesOffline)
{
	CMenus::SFriendAutoFollowState State;
	char aConnect[NETADDR_MAXSTRSIZE] = "";

	CMenus::StartFriendAutoFollow(State, "Alice", "Clan", "127.0.0.1:8303");
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, true, "127.0.0.1:8304", 11.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_TRUE(State.m_HasPendingAddress);
	EXPECT_FALSE(CMenus::FriendAutoFollowStep(State, false, "", 12.0f, 3, 2, aConnect, sizeof(aConnect)));
	EXPECT_FALSE(State.m_Active);
	EXPECT_FALSE(State.m_HasPendingAddress);
}

TEST(QmNewUiMenuGameplaySocialContract, FriendAutoFollowDistinguishesManualAndAutomaticConnects)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");

	EXPECT_NE(Header.find("enum class EConnectIntent"), std::string::npos);
	EXPECT_NE(Header.find("void Connect(const char *pAddress, EConnectIntent Intent = EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("if(Intent == EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("StopFriendAutoFollow(m_FriendAutoFollowState);"), std::string::npos);
	EXPECT_NE(Source.find("Connect(g_Config.m_UiServerAddress, EConnectIntent::AutoFollow)"), std::string::npos);
	const std::string FriendNotifyBody = FunctionBody(QmMenusSource, "void CMenus::RenderQmFunctionFriendNotifyContent(");
	ASSERT_FALSE(FriendNotifyBody.empty());
	EXPECT_NE(FriendNotifyBody.find("RenderValue(\"qmclient-friend-auto-follow-delay\", \"Auto-follow delay\""), std::string::npos);
	EXPECT_NE(FriendNotifyBody.find("&g_Config.m_QmFriendAutoFollowDelay, 0, 30, \"s\""), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, ShortServerNamesCoverKnownFamilies)
{
	CServerInfo Info{};
	char aBuf[sizeof(Info.m_aName)];

	str_copy(Info.m_aName, "KoG | China #12 - HappyHook [kog.tw]");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "China - HappyHook");

	str_copy(Info.m_aName, "Axiom 北京 普通 - CHN1O 钩累死");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "普通 - CHN1O 北京");

	str_copy(Info.m_aName, "DDNet CHN7 西安 - Moderate 中阶");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN2 上海 - Brutal 高阶");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶图 - CHN2 上海");

	// Axiom 尾部用地区而不是玩法模式(钩累死/AXRace)；名字里没有地区时只留区段标记。
	str_copy(Info.m_aName, "Axiom Novice - CHN12 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "简单 - CHN12");

	str_copy(Info.m_aName, "Axiom Insane - CHN7 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "疯狂 - CHN7");

	str_copy(Info.m_aName, "Axiom ⌬ 上海 ✦ 单人 - CHN1 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "单人 - CHN1 上海");

	str_copy(Info.m_aName, "Axiom Axiom ◇ 广州 ✦ 困难 - CHN9 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "困难 - CHN9 广州");

	str_copy(Info.m_aName, "Axiom Axiom ◇ 北京 ✦ 困难 - CHN10 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "困难 - CHN10 北京");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ DDmaX - CHN7 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典 - CHN7 广州");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ DDmaX.Pro 古典 - CHN7 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典 Pro - CHN7 广州");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 困难 - CHN7 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "困难 - CHN7 广州");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 活动 - CHN9 AXRace");
	str_copy(Info.m_aGameType, "AXRace");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "活动 - CHN9 广州");

	// 官方简中 Event 译作「活动」，英文写法也要认出来。
	str_copy(Info.m_aName, "DDNet CHN2 上海 - Event 活动");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "活动图 - CHN2 上海");

	str_copy(Info.m_aName, "Axiom ◇ 广州 ✦ 极限 - CHN9 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "极限 - CHN9 广州");

	str_copy(Info.m_aName, "Axiom ◇ 上海 ✦ 训练 - CHN2 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "训练 - CHN2 上海");

	str_copy(Info.m_aName, "Axiom ◇ 成都 ✦ 娱乐 - CHN12 钩累死");
	str_copy(Info.m_aGameType, "Gores");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "娱乐 - CHN12 成都");

	str_copy(Info.m_aName, "DDNet Moderate - CHN7 西安");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN2 上海 - DDmaX.Easy 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Easy - CHN2 上海");

	str_copy(Info.m_aName, "DDNet CHN7 西安 - DDmaX.Next");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Next - CHN7 西安");

	str_copy(Info.m_aName, "DDNet CHN3 宁波 - DDmaX.Pro 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Pro - CHN3 宁波");

	str_copy(Info.m_aName, "DDNet CHN4 成都 - DDmaX.Nut 古典");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Nut - CHN4 成都");

	// 只带中文写作的古典服同样不应落到「传统图」。
	str_copy(Info.m_aName, "DDNet CHN2 上海 - 古典 next");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "古典图 Next - CHN2 上海");

	str_copy(Info.m_aName, "DDNet CHN6 上海 - Oldschool 传统");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "传统图 - CHN6 上海");

	str_copy(Info.m_aName, "DDNet CHN6 上海 - Solo 单人");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "单人图 - CHN6 上海");

	str_copy(Info.m_aName, "DDNet CHN5 上海 - Dummy 分身");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "分身图 - CHN5 上海");

	str_copy(Info.m_aName, "DDNet Taiwan - Moderate");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "中阶图 - Taiwan");

	str_copy(Info.m_aName, "DDNet Taiwan - Brutal");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶图 - Taiwan");

	str_copy(Info.m_aName, "Brutal - CHN5 上海");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "高阶图 - CHN5 上海");

	str_copy(Info.m_aName, "Plain Server Name");
	str_copy(Info.m_aGameType, "DDraceNetwork");
	EXPECT_STREQ(CMenus::GetServerbrowserDisplayName(&Info, aBuf, sizeof(aBuf)), "Plain Server Name");
}

TEST(QmNewUiMenuGameplaySocialContract, ShortServerNamesKeepDisplayNameHighlightPath)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Source.find("g_Config.m_QmShortServerNames || (pItem->m_QuickSearchHit & IServerBrowser::QUICK_SERVERNAME)"), std::string::npos);
	EXPECT_NE(Source.find("PrintHighlighted(pDisplayServerName"), std::string::npos);
	EXPECT_EQ(Source.find("!g_Config.m_QmShortServerNames && g_Config.m_BrFilterString"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, CallVoteMapListShowsFinishedIcon)
{
	const std::string Source = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const size_t RenderPos = Source.find("bool CMenus::RenderServerControlServer(CUIRect MainView, bool UpdateScroll)");
	ASSERT_NE(RenderPos, std::string::npos);
	const size_t EndPos = Source.find("bool CMenus::RenderServerControlKick(CUIRect MainView", RenderPos);
	ASSERT_NE(EndPos, std::string::npos);
	const std::string Body = Source.substr(RenderPos, EndPos - RenderPos);

	EXPECT_NE(Body.find("ExtractMapName(pOption->m_aDescription"), std::string::npos);
	EXPECT_NE(Body.find("g_Config.m_BrIndicateFinished"), std::string::npos);
	EXPECT_NE(Body.find("pCurrentCommunity->HasRank(aMapName) == CServerInfo::RANK_RANKED"), std::string::npos);
	EXPECT_NE(Body.find("RenderFontIcon_QmIcon(Icon, EQmIcon::FLAG_CHECKERED, FONT_ICON_FLAG_CHECKERED"), std::string::npos);
	EXPECT_NE(Body.find("GameClient()->m_TClient.IsFavoriteMap(aMapName)"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, ClientSourceDoesNotUseChineseLocalizeKeys)
{
	const std::string HudEditorSource = ReadTextFile("src/game/client/components/hud_editor.cpp");
	const std::string MenusSource = ReadTextFile("src/game/client/components/menus.cpp");
	const std::string BrowserSource = ReadTextFile("src/game/client/components/menus_browser.cpp");
	const std::string DemoSource = ReadTextFile("src/game/client/components/menus_demo.cpp");
	const std::string IngameTouchSource = ReadTextFile("src/game/client/components/menus_ingame_touch_controls.cpp");
	const std::string IngameSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");
	const std::string SettingsSource = ReadTextFile("src/game/client/components/menus_settings.cpp");
	const std::string SettingsControlsSource = ReadTextFile("src/game/client/components/menus_settings_controls.cpp");
	const std::string Settings7Source = ReadTextFile("src/game/client/components/menus_settings7.cpp");
	const std::string StartSource = ReadTextFile("src/game/client/components/menus_start.cpp");
	const std::string PieMenuSource = ReadTextFile("src/game/client/components/pie_menu.cpp");
	const std::string ScoreboardSource = ReadTextFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(HudEditorSource.find("Localize(\"Position jump tip\")"), std::string::npos);
	EXPECT_NE(MenusSource.find("m_apSettingsTabs[SETTINGS_SOUND] = Localize(\"Sound\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"DDmaX Easy\")"), std::string::npos);
	EXPECT_NE(BrowserSource.find("Localize(\"Favorite map\")"), std::string::npos);
	EXPECT_NE(DemoSource.find("Localize(\"Screenshots directory\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Allow dummy\", \"Touch button visibilities\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Dummy connected\", \"Touch button visibilities\")"), std::string::npos);
	EXPECT_NE(IngameTouchSource.find("Localize(\"Spectate\", \"Predefined touch button behaviors\")"), std::string::npos);
	EXPECT_NE(IngameSource.find("Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(IngameSource.find("Localize(\"Dummies are not allowed on this server\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Show spectator cursor\")"), std::string::npos);
	EXPECT_NE(SettingsSource.find("Localize(\"Auto save chat log\")"), std::string::npos);
	EXPECT_NE(Settings7Source.find("Localize(\"Dummy\")"), std::string::npos);
	EXPECT_NE(Settings7Source.find("Localize(\"Dummy\")"), std::string::npos);
	EXPECT_NE(StartSource.find("Localize(\"(Update required)\")"), std::string::npos);
	EXPECT_NE(PieMenuSource.find("Localize(\"Spectate\")"), std::string::npos);
	EXPECT_NE(ScoreboardSource.find("Localize(\"Spectators\")"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, PieMenuSeparatesSelfRenameFromOtherPlayerActions)
{
	const std::string Source = ReadTextFile("src/game/client/components/pie_menu.cpp");
	const std::string FindNearestPlayer = FunctionBody(Source, "int CPieMenu::FindNearestPlayer()");
	const std::string OpenMenu = FunctionBody(Source, "void CPieMenu::OpenMenu()");
	const std::string OnInput = FunctionBody(Source, "bool CPieMenu::OnInput(");
	const std::string UpdateSelection = FunctionBody(Source, "void CPieMenu::UpdateSelection()");
	const std::string OnRender = FunctionBody(Source, "void CPieMenu::OnRender()");
	const std::string RenderCenterInfo = FunctionBody(Source, "void CPieMenu::RenderCenterInfo()");
	const std::string ExecuteRenameOption = FunctionBody(Source, "void CPieMenu::ExecuteRenameOption(");

	// Both local connections belong to the user and must never become inner-ring targets.
	EXPECT_NE(FindNearestPlayer.find("GameClient()->IsLocalClientId(i)"), std::string::npos);

	// A connected local identity and at least one usable ring are required to open the menu.
	EXPECT_NE(OpenMenu.find("Client()->State() != IClient::STATE_ONLINE"), std::string::npos);
	EXPECT_NE(OpenMenu.find("LocalClientId < 0 || LocalClientId >= MAX_CLIENTS"), std::string::npos);
	EXPECT_NE(OpenMenu.find("if(TargetId < 0 && m_vRenameQueue.empty())"), std::string::npos);

	// Without another player the hidden inner ring cannot be selected or triggered by number keys.
	EXPECT_NE(OnInput.find("if(!HasTargetPlayer())"), std::string::npos);
	EXPECT_NE(UpdateSelection.find("if(HasTargetPlayer() && MouseDistance <= OuterRadius)"), std::string::npos);
	EXPECT_NE(OnRender.find("if(HasTargetPlayer())"), std::string::npos);

	// Targetless mode displays self, and hovering the outer ring identifies rename as a self action.
	EXPECT_NE(RenderCenterInfo.find("const int DisplayClientId = HasTargetPlayer() ? m_TargetClientId : LocalClientId;"), std::string::npos);
	EXPECT_NE(RenderCenterInfo.find("Localize(\"Self\")"), std::string::npos);
	EXPECT_EQ(ExecuteRenameOption.find("m_TargetClientId"), std::string::npos);
}

TEST(QmNewUiMenuGameplaySocialContract, QmClientAxiomAutoLoginLivesInQmClientComponent)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.cpp");
	const std::string Header = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.h");
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientHeader = ReadTextFile("src/game/client/gameclient.h");
	const std::string QmConfigHeader = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string QmMenusSource = ReadTextFile("src/game/client/components/qmclient/menus_qmclient.cpp");
	const std::string IngameMenusSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Header.find("class CQmAxiomAutoLogin : public CComponent"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("CQmAxiomAutoLogin m_QmAxiomAutoLogin;"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendLogin()"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendDummyLogin()"), std::string::npos);
	EXPECT_NE(Source.find("SendChatOnConn(IClient::CONN_DUMMY"), std::string::npos);
	EXPECT_NE(Header.find("TrySendDummyLogin"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmAxiomDummyLoginPassword"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("qm_axiom_dummy_login_password"), std::string::npos);
	EXPECT_NE(QmMenusSource.find("Axiom dummy password"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyLoginAllowedThisServer"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyWasConnected"), std::string::npos);
	EXPECT_EQ(Source.find("if(DummyConnected && !m_DummyWasConnected)"), std::string::npos);
	EXPECT_NE(Source.find("m_DummyLoginAllowedThisServer = true;"), std::string::npos);
	EXPECT_NE(Source.find("if(!m_DummyLoginAllowedThisServer)"), std::string::npos);
	EXPECT_NE(Header.find("EnableDummyReconnectForServer"), std::string::npos);
	EXPECT_NE(Header.find("DisableDummyReconnectForServer"), std::string::npos);
	EXPECT_NE(Source.find("Client()->DummyConnect();"), std::string::npos);
	EXPECT_NE(IngameMenusSource.find("GameClient()->m_QmAxiomAutoLogin.EnableDummyReconnectForServer();"), std::string::npos);
	EXPECT_NE(IngameMenusSource.find("GameClient()->OnDummyManualDisconnect();"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("void OnDummyManualDisconnect() override;"), std::string::npos);
	const std::string GameClientSource = ReadTextFile("src/game/client/gameclient.cpp");
	const std::string ManualDisconnectBody = FunctionBody(GameClientSource, "void CGameClient::OnDummyManualDisconnect()");
	ASSERT_FALSE(ManualDisconnectBody.empty());
	EXPECT_NE(ManualDisconnectBody.find("m_QmAxiomAutoLogin.DisableDummyReconnectForServer();"), std::string::npos);
	const std::string ClientSource = ReadTextFile("src/engine/client/client.cpp");
	const std::string DummyDisconnectBody = FunctionBody(ClientSource, "void CClient::Con_DummyDisconnect(");
	ASSERT_FALSE(DummyDisconnectBody.empty());
	EXPECT_NE(DummyDisconnectBody.find("GameClient()->OnDummyManualDisconnect();"), std::string::npos);
	EXPECT_NE(DummyDisconnectBody.find("DummyDisconnect(nullptr);"), std::string::npos);
	EXPECT_NE(Source.find("bool CQmAxiomAutoLogin::IsAxiomCommunity() const"), std::string::npos);
	EXPECT_NE(Source.find("Client()->ServerInfo().m_aCommunityId"), std::string::npos);
	EXPECT_NE(Header.find("QMCLIENT_AXIOM_AUTO_LOGIN_SLOW_RETRY_SECONDS"), std::string::npos);
	EXPECT_NE(Header.find("SQmAxiomAutoLoginState m_AutoLoginState;"), std::string::npos);
	EXPECT_NE(Header.find("m_SlowRetryMode"), std::string::npos);
	EXPECT_NE(Header.find("m_HardFailed"), std::string::npos);
	EXPECT_NE(Header.find("QmScheduleAxiomAutoLoginRetry"), std::string::npos);
	EXPECT_NE(Source.find("QmClassifyAxiomLoginReply(pText)"), std::string::npos);
	EXPECT_NE(Source.find("QmApplyAxiomLoginReply"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Trying Axiom auto login\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Trying Axiom dummy auto login\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login succeeded\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login failed, retrying\")"), std::string::npos);
	EXPECT_NE(Source.find("Localize(\"Axiom auto login failed\")"), std::string::npos);

	EXPECT_EQ(TClientHeader.find("IsAxiomCommunity() const"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("ResetAxiomAutoLoginState"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("UpdateAxiomAutoLogin"), std::string::npos);
	EXPECT_EQ(TClientHeader.find("HandleAxiomAutoLoginMessage"), std::string::npos);
	EXPECT_EQ(TClientSource.find("TrySendAxiomLogin"), std::string::npos);
	EXPECT_EQ(TClientSource.find("HandleAxiomAutoLoginMessage"), std::string::npos);
}
