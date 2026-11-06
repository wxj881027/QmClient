#include <engine/client/steam_client_path.h>
#include <engine/client/steam_probe.h>

#include <gtest/gtest.h>

#include <array>
#include <new>
#include <string>
#include <string_view>

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

TEST(SteamDetectedPath, WindowsDirectoryAppendsCompleteExecutableName)
{
	const auto Path = steam_path::WindowsCandidate(" \"D:/测试目录/Steam\" ");
	ASSERT_TRUE(Path);
	EXPECT_STREQ(Path->data(), "D:\\测试目录\\Steam\\steam.exe");
}

TEST(SteamDetectedPath, QuotedExecutableDoesNotAppendAnotherName)
{
	const auto Path = steam_path::WindowsCandidate(" \"D:/Steam/STEAM.EXE\" ");
	ASSERT_TRUE(Path);
	EXPECT_STREQ(Path->data(), "D:\\Steam\\STEAM.EXE");
}

TEST(SteamDetectedPath, LongestCompleteDirectoryCandidateFitsWithTerminator)
{
	const std::string Directory = "D:\\" + std::string(1010, 'a');
	const auto Path = steam_path::WindowsCandidate(Directory.c_str());
	ASSERT_TRUE(Path);
	EXPECT_EQ(std::string_view(Path->data()), Directory + "\\steam.exe");
	EXPECT_EQ((*Path)[1023], '\0');
}

TEST(SteamDetectedPath, DirectoryWithoutRoomForWholeExecutableIsRejected)
{
	const std::string Directory = "D:\\" + std::string(1011, 'a');
	EXPECT_FALSE(steam_path::WindowsCandidate(Directory.c_str()));
}

TEST(SteamDetectedPath, CompleteExecutableMustFitIncludingTerminator)
{
	const std::string Path = "D:\\" + std::string(1016, 'a') + ".exe";
	const auto Candidate = steam_path::WindowsCandidate(Path.c_str());
	ASSERT_TRUE(Candidate);
	EXPECT_STREQ(Candidate->data(), Path.c_str());
	EXPECT_FALSE(steam_path::WindowsCandidate(("D:\\" + std::string(1017, 'a') + ".exe").c_str()));
}

TEST(SteamDetectedPath, EmptyOrQuoteOnlyCandidateDoesNotProduceExecutable)
{
	const char *apPaths[] = {nullptr, "", " \"\" "};
	for(const char *pPath : apPaths)
		EXPECT_FALSE(steam_path::WindowsCandidate(pPath));
}

TEST(SteamDetectedPath, HomeJoinPreservesCompleteUnicodePath)
{
	const auto Path = steam_path::Join("/home/测试", "/.steam/steam/steam.sh");
	ASSERT_TRUE(Path);
	EXPECT_STREQ(Path->data(), "/home/测试/.steam/steam/steam.sh");
}

TEST(SteamDetectedPath, HomeJoinRequiresRoomForWholeSuffixAndTerminator)
{
	const std::string Suffix = "/.steam/steam/steam.sh";
	const std::string Base(1023 - Suffix.size(), 'a');
	const auto Path = steam_path::Join(Base, Suffix);
	ASSERT_TRUE(Path);
	EXPECT_EQ(std::string_view(Path->data()), Base + Suffix);
	EXPECT_FALSE(steam_path::Join(Base + "a", Suffix));
	EXPECT_FALSE(steam_path::Join(std::string(1024, 'a'), Suffix));
}

TEST(SteamDetectedPath, JoinRejectsEmbeddedNullAndEmptyBase)
{
	EXPECT_FALSE(steam_path::Join(std::string_view("a\0b", 3), "/steam"));
	EXPECT_FALSE(steam_path::Join("/home", std::string_view("/a\0b", 4)));
	EXPECT_FALSE(steam_path::Join("", "/steam"));
}

TEST(SteamDetectedPath, CopyPublishesAllBytesWhenCapacityExactlyFits)
{
	char aPath[11] = {};
	EXPECT_TRUE(steam_path::Copy("/bin/steam", aPath, sizeof(aPath)));
	EXPECT_STREQ(aPath, "/bin/steam");
}

TEST(SteamDetectedPath, InsufficientOutputCapacityPreservesExistingBytes)
{
	std::array<char, 16> Buffer;
	Buffer.fill('q');
	const auto Previous = Buffer;
	for(const int Capacity : {0, -1, 1, 10})
	{
		SCOPED_TRACE(Capacity);
		EXPECT_FALSE(steam_path::Copy("/bin/steam", Buffer.data(), Capacity));
		EXPECT_EQ(Buffer, Previous);
	}
	EXPECT_FALSE(steam_path::Copy("/bin/steam", nullptr, 16));
}

TEST(SteamDetectedPath, CopyRejectsEmptyOrEmbeddedNullWithoutPublishingPrefix)
{
	char aBuffer[16] = "previous";
	EXPECT_FALSE(steam_path::Copy("", aBuffer, sizeof(aBuffer)));
	EXPECT_FALSE(steam_path::Copy(std::string_view("a\0b", 3), aBuffer, sizeof(aBuffer)));
	EXPECT_STREQ(aBuffer, "previous");
}

TEST(SteamDetectedPath, CopyAllowsOverlappingSourceAndOutput)
{
	char aBuffer[32] = "prefix/bin/steam";
	EXPECT_TRUE(steam_path::Copy(std::string_view(aBuffer + 6), aBuffer, sizeof(aBuffer)));
	EXPECT_STREQ(aBuffer, "/bin/steam");
}

TEST(SteamDetectedPath, InvalidCapacityDoesNotQueryFilesystem)
{
	char aBuffer[8] = "old";
	const auto Validate = [](const char *) {
		ADD_FAILURE() << "invalid output reached filesystem";
		return true;
	};
	EXPECT_FALSE(steam_path::Accept("/bin/steam", aBuffer, sizeof(aBuffer), Validate));
	EXPECT_FALSE(steam_path::Accept("/bin/steam", nullptr, 1024, Validate));
	EXPECT_FALSE(steam_path::Accept(nullptr, aBuffer, sizeof(aBuffer), Validate));
	EXPECT_FALSE(steam_path::Accept("", aBuffer, sizeof(aBuffer), Validate));
	EXPECT_STREQ(aBuffer, "old");
}

TEST(SteamDetectedPath, MissingFilePreservesPreviousOutput)
{
	char aBuffer[32] = "previous";
	int Queries = 0;
	EXPECT_FALSE(steam_path::Accept("/bin/steam", aBuffer, sizeof(aBuffer), [&](const char *pPath) {
		++Queries;
		EXPECT_STREQ(pPath, "/bin/steam");
		return false;
	}));
	EXPECT_EQ(Queries, 1);
	EXPECT_STREQ(aBuffer, "previous");
}

TEST(SteamDetectedPath, ExistingFileIsValidatedAndPublishedAsTheSameCompletePath)
{
	char aBuffer[64] = "previous";
	int Queries = 0;
	EXPECT_TRUE(steam_path::Accept("/home/测试/steam", aBuffer, sizeof(aBuffer), [&](const char *pPath) {
		++Queries;
		EXPECT_STREQ(pPath, "/home/测试/steam");
		return true;
	}));
	EXPECT_EQ(Queries, 1);
	EXPECT_STREQ(aBuffer, "/home/测试/steam");
}

TEST(SteamDetectedPath, FilesystemPreparationExceptionDoesNotPublishCandidate)
{
	char aBuffer[32] = "previous";
	EXPECT_THROW(steam_path::Accept("/bin/steam", aBuffer, sizeof(aBuffer), [](const char *) -> bool { throw std::bad_alloc(); }), std::bad_alloc);
	EXPECT_STREQ(aBuffer, "previous");
}
