#include <base/crash_output.h>

#include <gtest/gtest.h>

#include <array>
#include <string_view>

TEST(CrashOutputPath, SidecarReplacesOnlyFinalExtension)
{
	std::array<char, 64> aPath;
	ASSERT_TRUE(crash_output::MakeSidecarPath("dir.name/game.old.RPT", "_fatal_report.txt", aPath.data(), aPath.size()));
	EXPECT_STREQ(aPath.data(), "dir.name/game.old_fatal_report.txt");
}

TEST(CrashOutputPath, BackslashDirectoryDotsAreNotFileExtensions)
{
	std::array<char, 64> aPath;
	ASSERT_TRUE(crash_output::MakeSidecarPath("C:\\dir.name\\game", "_fatal_dump.dmp", aPath.data(), aPath.size()));
	EXPECT_STREQ(aPath.data(), "C:\\dir.name\\game_fatal_dump.dmp");
}

TEST(CrashOutputPath, HiddenFileKeepsExistingSidecarNamingRule)
{
	std::array<char, 64> aPath;
	ASSERT_TRUE(crash_output::MakeSidecarPath("dir/.RPT", "_fatal_report.txt", aPath.data(), aPath.size()));
	EXPECT_STREQ(aPath.data(), "dir/_fatal_report.txt");
}

TEST(CrashOutputPath, ConfirmationSuffixPreservesFinalFileExtension)
{
	std::array<char, 64> aPath;
	ASSERT_TRUE(crash_output::AppendPathSuffix("game_fatal_report.txt", ".confirmed", aPath.data(), aPath.size()));
	EXPECT_STREQ(aPath.data(), "game_fatal_report.txt.confirmed");
}

TEST(CrashOutputPath, EmptySuffixCopiesCompletePath)
{
	std::array<char, 16> aPath;
	ASSERT_TRUE(crash_output::AppendPathSuffix("game.RPT", {}, aPath.data(), aPath.size()));
	EXPECT_STREQ(aPath.data(), "game.RPT");
}

TEST(CrashOutputPath, ExactCapacityIncludesTerminatingNul)
{
	std::array<char, 6> aPath;
	ASSERT_TRUE(crash_output::MakeSidecarPath("a.RPT", ".txt", aPath.data(), aPath.size()));
	EXPECT_STREQ(aPath.data(), "a.txt");
}

TEST(CrashOutputPath, OneByteShortClearsOutputWithoutWritingPastIt)
{
	std::array<char, 7> aStorage;
	aStorage.fill('x');
	EXPECT_FALSE(crash_output::MakeSidecarPath("a.RPT", ".txt", aStorage.data() + 1, 5));
	EXPECT_EQ(aStorage[0], 'x');
	EXPECT_EQ(aStorage[1], '\0');
	EXPECT_EQ(aStorage[6], 'x');
}

TEST(CrashOutputPath, LongCommonPrefixCannotProduceTruncatedReportDumpAliases)
{
	std::array<char, 16> aReport, aDump;
	aReport.fill('x');
	aDump.fill('x');
	EXPECT_FALSE(crash_output::MakeSidecarPath("long-base.RPT", "_fatal_report.txt", aReport.data(), aReport.size()));
	EXPECT_FALSE(crash_output::MakeSidecarPath("long-base.RPT", "_fatal_dump.dmp", aDump.data(), aDump.size()));
	EXPECT_EQ(aReport[0], '\0');
	EXPECT_EQ(aDump[0], '\0');
}

TEST(CrashOutputPath, InteriorNulInBorrowedBaseIsRejected)
{
	std::array<char, 32> aPath;
	EXPECT_FALSE(crash_output::MakeSidecarPath(std::string_view("ab\0cd.RPT", 9), ".txt", aPath.data(), aPath.size()));
	EXPECT_EQ(aPath[0], '\0');
}

TEST(CrashOutputPath, InteriorNulInSuffixIsRejected)
{
	std::array<char, 32> aPath;
	EXPECT_FALSE(crash_output::AppendPathSuffix("game.RPT", std::string_view("x\0y", 3), aPath.data(), aPath.size()));
	EXPECT_EQ(aPath[0], '\0');
}

TEST(CrashOutputPath, MissingFilenameIsRejected)
{
	std::array<char, 32> aPath;
	EXPECT_FALSE(crash_output::MakeSidecarPath({}, ".txt", aPath.data(), aPath.size()));
	EXPECT_EQ(aPath[0], '\0');
	EXPECT_FALSE(crash_output::MakeSidecarPath("dir/", ".txt", aPath.data(), aPath.size()));
	EXPECT_EQ(aPath[0], '\0');
}

TEST(CrashOutputPath, AbsentOutputCapacityIsNotAccessed)
{
	char Sentinel = 'x';
	EXPECT_FALSE(crash_output::AppendPathSuffix("game.RPT", ".confirmed", &Sentinel, 0));
	EXPECT_EQ(Sentinel, 'x');
	EXPECT_FALSE(crash_output::AppendPathSuffix("game.RPT", ".confirmed", nullptr, 32));
}

TEST(CrashOutputPath, Utf8BytesAreCopiedWholeOrRejectedWithoutTruncation)
{
	// UTF-8的“猫”，保持测试不依赖编译器执行字符集。
	constexpr char aBase[] = "\xe7\x8c\xab.RPT";
	std::array<char, 6> aExact;
	ASSERT_TRUE(crash_output::MakeSidecarPath(aBase, ".x", aExact.data(), aExact.size()));
	EXPECT_STREQ(aExact.data(), "\xe7\x8c\xab.x");
	std::array<char, 5> aShort;
	EXPECT_FALSE(crash_output::MakeSidecarPath(aBase, ".x", aShort.data(), aShort.size()));
	EXPECT_EQ(aShort[0], '\0');
}
