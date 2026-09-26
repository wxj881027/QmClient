// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <gtest/gtest.h>

#include <string>

TEST(DDNet199Sync, SortsServerBrowserByFavorites)
{
	const std::string Interface = ReadTestSourceFile("src/engine/serverbrowser.h");
	const std::string Browser = ReadTestSourceFile("src/engine/client/serverbrowser.cpp");
	const std::string Menus = ReadTestSourceFile("src/game/client/components/menus_browser.cpp");

	EXPECT_NE(Interface.find("SORT_FAVORITES"), std::string::npos);
	EXPECT_NE(Browser.find("SortCompareFavoritesNumPlayersAndPing"), std::string::npos);
	EXPECT_NE(Browser.find("m_Info.m_Favorite != TRISTATE::NONE"), std::string::npos);
	EXPECT_NE(Menus.find("{COL_FLAG_FAV, IServerBrowser::SORT_FAVORITES"), std::string::npos);
}

TEST(DDNet199Sync, UsesLoadedMapForDiscordActivity)
{
	const std::string Client = ReadTestSourceFile("src/engine/client/client.cpp");
	const std::string Discord = ReadTestSourceFile("src/engine/client/discord.cpp");
	const std::string DiscordInterface = ReadTestSourceFile("src/engine/discord.h");

	EXPECT_NE(Client.find("void CClient::SetCurrentServerInfo(const CServerInfo &ServerInfo)"), std::string::npos);
	EXPECT_NE(Client.find("m_pMap->IsLoaded()"), std::string::npos);
	EXPECT_NE(Client.find("str_copy(m_CurrentServerInfo.m_aMap, GetCurrentMap());"), std::string::npos);
	EXPECT_NE(Client.find("m_CurrentServerInfo.m_MapCrc = m_pMap->Crc();"), std::string::npos);
	EXPECT_NE(Client.find("m_CurrentServerInfo.m_MapSize = m_pMap->Size();"), std::string::npos);
	EXPECT_EQ(Client.find("GameClient()->Map()"), std::string::npos);
	EXPECT_NE(Client.find("Discord()->SetGameInfo(m_CurrentServerInfo, Registered);"), std::string::npos);
	EXPECT_NE(Discord.find("str_copy(m_Activity.state, ServerInfo.m_aMap"), std::string::npos);
	EXPECT_EQ(DiscordInterface.find("const char *pMapName"), std::string::npos);
}

TEST(DDNet199Sync, SupportsUnbufferedQuadClippingAndTuneZoneColors)
{
	const std::string Math = ReadTestSourceFile("src/base/math.h");
	const std::string MapImages = ReadTestSourceFile("src/game/client/components/mapimages.cpp");
	const std::string RenderLayer = ReadTestSourceFile("src/game/map/render_layer.cpp");
	const std::string RenderMap = ReadTestSourceFile("src/game/map/render_map.cpp");
	const std::string RenderMapHeader = ReadTestSourceFile("src/game/map/render_map.h");

	EXPECT_NE(RenderLayer.find("// create clip region for unbuffered backends"), std::string::npos);
	EXPECT_NE(RenderLayer.find("CalculateClipping(QuadCluster);"), std::string::npos);
	EXPECT_NE(Math.find("constexpr float normalized_golden_angle"), std::string::npos);
	EXPECT_NE(MapImages.find("IGraphics::CTextureHandle CMapImages::GetTuneColors()"), std::string::npos);
	EXPECT_NE(MapImages.find("ColorizeWithHueRect"), std::string::npos);
	EXPECT_NE(RenderMap.find("RenderTunemap"), std::string::npos);
	EXPECT_NE(RenderMapHeader.find("uint8_t TuneNumberToColorIndex"), std::string::npos);
	EXPECT_NE(RenderMapHeader.find("uint8_t TileTextureIndex"), std::string::npos);
	EXPECT_NE(RenderMapHeader.find("ColorRGBA TuneColorIndexToColor"), std::string::npos);
}

TEST(DDNet199Sync, KeepsPredictEventsDisabledAndPopupSelectionHighlighted)
{
	const std::string Config = ReadTestSourceFile("src/engine/shared/config_variables.h");
	const std::string Settings = ReadTestSourceFile("src/game/client/components/menus_settings.cpp");
	const std::string Scoreboard = ReadTestSourceFile("src/game/client/components/scoreboard.cpp");

	EXPECT_NE(Config.find("MACRO_CONFIG_INT(ClPredictEvents, cl_predict_events, 0, 0, 1"), std::string::npos);
	EXPECT_NE(Settings.find("Predict events (experimental)"), std::string::npos);
	EXPECT_NE(Scoreboard.find("Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == ClientId"), std::string::npos);
}

TEST(DDNet199Sync, AddsDemoBrowserTooltips)
{
	const std::string Source = ReadTestSourceFile("src/game/client/components/menus_demo.cpp");

	EXPECT_NE(Source.find("DoToolTip(&s_RefreshButton, &RefreshButton"), std::string::npos);
	EXPECT_NE(Source.find("DoToolTip(&s_PlayButton, &PlayButton"), std::string::npos);
	EXPECT_NE(Source.find("DoToolTip(&s_RenameButton, &RenameButton"), std::string::npos);
	EXPECT_NE(Source.find("DoToolTip(&s_DeleteButton, &DeleteButton"), std::string::npos);
	EXPECT_NE(Source.find("DoToolTip(&s_RenderButton, &RenderButton"), std::string::npos);
}

TEST(DDNet199Sync, RejectsEscapeBindsAndUsesCurrentHookState)
{
	const std::string Binds = ReadTestSourceFile("src/game/client/components/binds.cpp");
	const std::string Console = ReadTestSourceFile("src/game/client/components/console.cpp");
	const std::string Players = ReadTestSourceFile("src/game/client/components/players.cpp");

	EXPECT_NE(Binds.find("if(Key == KEY_ESCAPE)"), std::string::npos);
	EXPECT_NE(Console.find("if(Key == KEY_ESCAPE)"), std::string::npos);
	EXPECT_NE(Players.find("if(pPlayerChar->m_HookState <= 0)"), std::string::npos);
	EXPECT_EQ(Players.find("if(pPrevChar->m_HookState <= 0 || pPlayerChar->m_HookState <= 0)"), std::string::npos);
}

TEST(DDNet199Sync, UsesLocalTuningForUnpredictedHookCollision)
{
	const std::string Players = ReadTestSourceFile("src/game/client/components/players.cpp");

	EXPECT_NE(Players.find("const CCharacterCore &PlayerCore = GameClient()->m_aClients[ClientId].m_IsPredicted"), std::string::npos);
	EXPECT_NE(Players.find("GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_Predicted"), std::string::npos);
	EXPECT_NE(Players.find("PlayerCore.m_Tuning.m_HookLength"), std::string::npos);
	EXPECT_NE(Players.find("PlayerCore.m_Tuning.m_HookFireSpeed"), std::string::npos);
}

TEST(DDNet20TouchDismiss, ComponentsCanConsumeTouchFingersInInputOrder)
{
	const std::string Component = ReadTestSourceFile("src/game/client/component.h");
	const std::string GameClient = ReadTestSourceFile("src/game/client/gameclient.cpp");
	const std::string TouchControls = ReadTestSourceFile("src/game/client/components/touch_controls.cpp");

	EXPECT_NE(Component.find("std::vector<IInput::CTouchFingerState> &vTouchFingerStates"), std::string::npos);
	EXPECT_NE(GameClient.find("std::vector<IInput::CTouchFingerState> vTouchFingerStates = Input()->TouchFingerStates();"), std::string::npos);
	EXPECT_NE(TouchControls.find("vTouchFingerStates.clear();"), std::string::npos);
}

TEST(DDNet20TouchDismiss, MotdDismissesOnlyNewTouchesOutsideItsRect)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/motd.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/motd.cpp");

	EXPECT_NE(Header.find("std::optional<CUIRect> m_TouchRect"), std::string::npos);
	EXPECT_NE(Header.find("std::optional<IInput::CTouchFinger> m_DismissTouchFinger"), std::string::npos);
	EXPECT_NE(Source.find("State.m_PressTime > m_ShownSince && !m_TouchRect->Inside(State.m_Position)"), std::string::npos);
	EXPECT_NE(Source.find("m_DismissTouchFinger = DismissFinger->m_Finger"), std::string::npos);
}

TEST(DDNet20TouchDismiss, ImportantAlertsExposeTouchDismissalAfterMinimumDuration)
{
	const std::string Header = ReadTestSourceFile("src/game/client/components/important_alert.h");
	const std::string Source = ReadTestSourceFile("src/game/client/components/important_alert.cpp");

	EXPECT_NE(Header.find("std::optional<CUIRect> m_DismissTouchRect"), std::string::npos);
	EXPECT_NE(Source.find("SecondsActive() < MINIMUM_ACTIVE_SECONDS"), std::string::npos);
	EXPECT_NE(Source.find("m_DismissTouchRect->Inside(State.m_Position)"), std::string::npos);
}
