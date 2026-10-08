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
#include <test/support/qmclient_source_contract_test.h>
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

TEST(QmNewUiMenuGameplaySocialContract, FriendAutoFollowDistinguishesManualAndAutomaticConnects)
{
	const std::string Header = ReadTextFile("src/game/client/components/menus.h");
	const std::string Source = ReadTextFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Header.find("enum class EConnectIntent"), std::string::npos);
	EXPECT_NE(Header.find("void Connect(const char *pAddress, EConnectIntent Intent = EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("if(Intent == EConnectIntent::Manual)"), std::string::npos);
	EXPECT_NE(Source.find("StopFriendAutoFollow(m_FriendAutoFollowState);"), std::string::npos);
	EXPECT_NE(Source.find("Connect(g_Config.m_UiServerAddress, EConnectIntent::AutoFollow)"), std::string::npos);
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

TEST(QmNewUiMenuGameplaySocialContract, QmClientAxiomAutoLoginLivesInQmClientComponent)
{
	const std::string Source = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.cpp");
	const std::string Header = ReadTextFile("src/game/client/components/qmclient/axiom_auto_login.h");
	const std::string TClientHeader = ReadTextFile("src/game/client/components/tclient/tclient.h");
	const std::string TClientSource = ReadTextFile("src/game/client/components/tclient/tclient.cpp");
	const std::string GameClientHeader = ReadTextFile("src/game/client/gameclient.h");
	const std::string QmConfigHeader = ReadTextFile("src/engine/shared/config_variables_qmclient.h");
	const std::string IngameMenusSource = ReadTextFile("src/game/client/components/menus_ingame.cpp");

	EXPECT_NE(Header.find("class CQmAxiomAutoLogin : public CComponent"), std::string::npos);
	EXPECT_NE(GameClientHeader.find("CQmAxiomAutoLogin m_QmAxiomAutoLogin;"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendLogin()"), std::string::npos);
	EXPECT_NE(Source.find("void CQmAxiomAutoLogin::TrySendDummyLogin()"), std::string::npos);
	EXPECT_NE(Source.find("SendChatOnConn(IClient::CONN_DUMMY"), std::string::npos);
	EXPECT_NE(Header.find("TrySendDummyLogin"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("QmAxiomDummyLoginPassword"), std::string::npos);
	EXPECT_NE(QmConfigHeader.find("qm_axiom_dummy_login_password"), std::string::npos);
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
