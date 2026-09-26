// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "test.h"

#include <gtest/gtest.h>

#include <string>

TEST(SteamPresence, UsesClientNameForVisibleStatus)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/steam.cpp");

	EXPECT_NE(Source.find("#include <game/version.h>"), std::string::npos);
	EXPECT_NE(Source.find("SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, \"status\", CLIENT_NAME);"), std::string::npos);
	EXPECT_NE(Source.find("SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, \"map\", pMapName);"), std::string::npos);
	EXPECT_EQ(Source.find("SteamAPI_ISteamFriends_SetRichPresence(m_pSteamFriends, \"status\", pMapName);"), std::string::npos);
}

TEST(SteamPresence, KeepsBrandPresenceWhenGameInfoClears)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/steam.cpp");

	const size_t ResetClientPresencePos = Source.find("void ResetClientPresence()");
	ASSERT_NE(ResetClientPresencePos, std::string::npos);
	const size_t PublicSectionPos = Source.find("public:", ResetClientPresencePos);
	ASSERT_NE(PublicSectionPos, std::string::npos);
	const std::string ResetClientPresenceBody = Source.substr(ResetClientPresencePos, PublicSectionPos - ResetClientPresencePos);
	EXPECT_NE(ResetClientPresenceBody.find("SteamAPI_ISteamFriends_ClearRichPresence(m_pSteamFriends);"), std::string::npos);
	EXPECT_NE(ResetClientPresenceBody.find("SetClientPresence();"), std::string::npos);

	const size_t ConstructorPos = Source.find("CSteam()");
	ASSERT_NE(ConstructorPos, std::string::npos);
	const size_t DestructorPos = Source.find("~CSteam()", ConstructorPos);
	ASSERT_NE(DestructorPos, std::string::npos);
	const std::string ConstructorBody = Source.substr(ConstructorPos, DestructorPos - ConstructorPos);
	EXPECT_NE(ConstructorBody.find("ResetClientPresence();"), std::string::npos);

	const size_t ClearGameInfoPos = Source.find("void ClearGameInfo() override");
	ASSERT_NE(ClearGameInfoPos, std::string::npos);
	const size_t SetGameInfoPos = Source.find("void SetGameInfo", ClearGameInfoPos);
	ASSERT_NE(SetGameInfoPos, std::string::npos);
	const std::string ClearGameInfoBody = Source.substr(ClearGameInfoPos, SetGameInfoPos - ClearGameInfoPos);
	EXPECT_NE(ClearGameInfoBody.find("ResetClientPresence();"), std::string::npos);
}

TEST(SteamPresence, OpensSteamInBackground)
{
	const std::string Source = ReadTestSourceFile("src/engine/client/steam.cpp");
	const size_t FunctionPos = Source.find("bool SteamOpenClient()");
	ASSERT_NE(FunctionPos, std::string::npos);
	const size_t FunctionEnd = Source.find("\n}\n\nISteam *CreateSteam", FunctionPos);
	ASSERT_NE(FunctionEnd, std::string::npos);
	const std::string FunctionBody = Source.substr(FunctionPos, FunctionEnd - FunctionPos);
	EXPECT_NE(FunctionBody.find("EShellExecuteWindowState::BACKGROUND"), std::string::npos);
	EXPECT_NE(FunctionBody.find("shell_execute(\"steam.exe\""), std::string::npos);
	EXPECT_NE(Source.find("STEAM_SILENT_ARGUMENT = \"-silent\""), std::string::npos);
	EXPECT_EQ(FunctionBody.find("steam://open/main"), std::string::npos);
	EXPECT_EQ(FunctionBody.find("open_link("), std::string::npos);
}

TEST(SteamPresence, AutoLaunchDefaultsToDisabled)
{
	const std::string Source = ReadTestSourceFile("src/engine/shared/config_variables_qmclient.h");
	EXPECT_NE(Source.find("MACRO_CONFIG_INT(QmSteamAutoLaunch, qm_steam_auto_launch, 0, 0, 1"), std::string::npos);
}
