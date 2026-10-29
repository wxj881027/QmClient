#include <engine/client/steam_client_path.h>
#include <engine/client/steam_probe.h>

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

#if defined(CONF_FAMILY_WINDOWS)
TEST(SteamRegistryString, TerminatedUtf16ValuePreservesTheWholePath)
{
	const wchar_t aValue[] = L"D:\\测试\\Steam\\steam.exe";
	const auto Value = SteamProbeDecodeRegistryString(aValue, sizeof(aValue) / sizeof(wchar_t), sizeof(aValue));
	ASSERT_TRUE(Value);
	EXPECT_EQ(*Value, "D:\\测试\\Steam\\steam.exe");
}

TEST(SteamRegistryString, FullBufferWithoutTerminatorIsRejected)
{
	const wchar_t aValue[] = {L'a', L'b', L'c', L'd'};
	EXPECT_FALSE(SteamProbeDecodeRegistryString(aValue, 4, sizeof(aValue)));
}

TEST(SteamRegistryString, TerminatorOutsideReturnedBytesDoesNotCompleteTheValue)
{
	const wchar_t aValue[] = {L'a', L'b', L'\0'};
	EXPECT_FALSE(SteamProbeDecodeRegistryString(aValue, 3, 2 * sizeof(wchar_t)));
}

TEST(SteamRegistryString, OddOrOversizedByteCountIsRejected)
{
	const wchar_t aValue[] = L"x";
	EXPECT_FALSE(SteamProbeDecodeRegistryString(aValue, 2, sizeof(aValue) - 1));
	EXPECT_FALSE(SteamProbeDecodeRegistryString(aValue, 2, sizeof(aValue) + sizeof(wchar_t)));
	EXPECT_FALSE(SteamProbeDecodeRegistryString(aValue, 2, 0));
	EXPECT_FALSE(SteamProbeDecodeRegistryString(nullptr, 2, sizeof(aValue)));
}

TEST(SteamRegistryString, InvalidUtf16DoesNotPublishAPath)
{
	const wchar_t aValue[] = {static_cast<wchar_t>(0xd800), L'\0'};
	EXPECT_FALSE(SteamProbeDecodeRegistryString(aValue, 2, sizeof(aValue)));
}
#endif
