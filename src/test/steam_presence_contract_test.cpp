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

TEST(SteamPresence, RestartsExternalLaunchThroughSteam)
{
	const std::string SteamSource = ReadTestSourceFile("src/engine/client/steam.cpp");
	const size_t AppIdPos = SteamSource.find("constexpr uint32_t STEAM_APP_ID = 412220;");
	ASSERT_NE(AppIdPos, std::string::npos);
	const size_t RestartFunctionPos = SteamSource.find("bool SteamRestartAppIfNecessary()", AppIdPos);
	ASSERT_NE(RestartFunctionPos, std::string::npos);
	const size_t RestartApiPos = SteamSource.find("SteamAPI_RestartAppIfNecessary(STEAM_APP_ID)", RestartFunctionPos);
	ASSERT_NE(RestartApiPos, std::string::npos);

	const std::string ClientSource = ReadTestSourceFile("src/engine/client/client.cpp");
	EXPECT_NE(ClientSource.find("g_Config.m_QmSteamAutoLaunch && SteamRestartAppIfNecessary()"), std::string::npos);
	const size_t ParseArgumentsPos = ClientSource.find("pConsole->ParseArguments(argc - 1, &argv[1]);");
	ASSERT_NE(ParseArgumentsPos, std::string::npos);
	const size_t RestartCallPos = ClientSource.find("SteamRestartAppIfNecessary()", ParseArgumentsPos);
	ASSERT_NE(RestartCallPos, std::string::npos);
	const size_t CreateSteamPos = ClientSource.find("ISteam *pSteam = CreateSteam();", RestartCallPos);
	ASSERT_NE(CreateSteamPos, std::string::npos);
	EXPECT_LT(RestartCallPos, CreateSteamPos);
}
