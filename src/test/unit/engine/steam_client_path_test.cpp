#include <engine/client/steam_client_path.h>

#include <gtest/gtest.h>

TEST(SteamManualPath, AcceptsQuotedCustomDriveAndNormalizesSeparators)
{
	EXPECT_EQ(SteamManualPathCandidate(" \"D:/Games/Steam Client/Steam.EXE\" \t", ESteamClientPlatform::WINDOWS), "D:\\Games\\Steam Client\\Steam.EXE");
}

TEST(SteamManualPath, RejectsRelativeNetworkOtherProgramsAndArguments)
{
	for(const char *pPath : {"steam.exe", "D:steam.exe", "\\\\server\\Steam\\steam.exe", "D:/Games/tool.exe", "D:/Games/steam.exe -silent", "\"D:/Games/steam.exe\" -silent", "D:/Games/steam.exe\n"})
	{
		SCOPED_TRACE(pPath);
		EXPECT_TRUE(SteamManualPathCandidate(pPath, ESteamClientPlatform::WINDOWS).empty());
	}
}

TEST(SteamManualPath, EmptyAndUnmatchedQuotesDoNotProduceLaunchCandidates)
{
	const char *apPaths[] = {nullptr, "", " \t", "\"\"", "\"D:/Steam/steam.exe"};
	for(const char *pPath : apPaths)
	{
		SCOPED_TRACE(pPath != nullptr ? pPath : "null");
		EXPECT_TRUE(SteamManualPathCandidate(pPath, ESteamClientPlatform::WINDOWS).empty());
	}
}

TEST(SteamManualPath, UnixAndMacUseTheirOwnClientNames)
{
	EXPECT_EQ(SteamManualPathCandidate("/home/player/.steam/steam/steam.sh", ESteamClientPlatform::UNIX), "/home/player/.steam/steam/steam.sh");
	EXPECT_EQ(SteamManualPathCandidate("/usr/bin/steam", ESteamClientPlatform::UNIX), "/usr/bin/steam");
	EXPECT_EQ(SteamManualPathCandidate("/Applications/Steam.app", ESteamClientPlatform::MACOS), "/Applications/Steam.app");
	EXPECT_TRUE(SteamManualPathCandidate("/usr/bin/other", ESteamClientPlatform::UNIX).empty());
	EXPECT_TRUE(SteamManualPathCandidate("/Applications/Other.app", ESteamClientPlatform::MACOS).empty());
}
