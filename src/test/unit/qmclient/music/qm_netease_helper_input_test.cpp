#include <gtest/gtest.h>
#include <qm/hooks/netease/qm_netease_helper_input.h>

using namespace QmNeteaseHelper;

TEST(QmNeteaseHelperInput, AcceptsOnlyWholeCharactersWithinTheReportedByteCapacity)
{
	EXPECT_TRUE(ValidWideByteCount(0, 0));
	EXPECT_TRUE(ValidWideByteCount(2 * sizeof(wchar_t), 2 * sizeof(wchar_t)));
	EXPECT_FALSE(ValidWideByteCount(sizeof(wchar_t) + 1, 8 * sizeof(wchar_t)));
	EXPECT_FALSE(ValidWideByteCount(3 * sizeof(wchar_t), 2 * sizeof(wchar_t)));
}

TEST(QmNeteaseHelperInput, CopiesAnUnterminatedNativeBufferOnlyWithinItsReturnedRange)
{
	const wchar_t aText[] = {L'a', L'b'};
	EXPECT_EQ(CopyWideBytes(aText, sizeof(aText), 2), L"ab");
}

TEST(QmNeteaseHelperInput, DoesNotCopyUnreturnedTrailingData)
{
	const wchar_t aText[] = {L'a', L'b', L'c', L'd'};
	EXPECT_EQ(CopyWideBytes(aText, 2 * sizeof(wchar_t), 4), L"ab");
}

TEST(QmNeteaseHelperInput, StopsAtAnEmbeddedTerminatorWithinTheReturnedRange)
{
	const wchar_t aText[] = {L'a', L'\0', L'b', L'c'};
	EXPECT_EQ(CopyWideBytes(aText, sizeof(aText), 4), L"a");
}

TEST(QmNeteaseHelperInput, RejectsAPartialWideCharacterBeforeReadingTheBuffer)
{
	const wchar_t aText[] = {L'a'};
	EXPECT_TRUE(CopyWideBytes(aText, sizeof(wchar_t) + 1, 1).empty());
}

TEST(QmNeteaseHelperInput, RejectsAResultThatGrewBeyondTheAllocatedBuffer)
{
	const wchar_t aText[] = {L'a'};
	EXPECT_TRUE(CopyWideBytes(aText, 2 * sizeof(wchar_t), 1).empty());
}

TEST(QmNeteaseHelperInput, NullInputAndAnEmptyReportedRangeProduceNoText)
{
	EXPECT_TRUE(CopyWideBytes(nullptr, 0, 0).empty());
	const wchar_t aText[] = {L'a'};
	EXPECT_TRUE(CopyWideBytes(aText, 0, 1).empty());
}

TEST(QmNeteaseHelperInput, QuotesASpacedDirectoryWithoutChangingItsContents)
{
	EXPECT_EQ(QuoteDirectoryArgument(L"C:\\Music App"), L"\"C:\\Music App\"");
}

TEST(QmNeteaseHelperInput, EscapesTheTrailingBackslashOfADriveRoot)
{
	EXPECT_EQ(QuoteDirectoryArgument(L"C:\\"), L"\"C:\\\\\"");
}

TEST(QmNeteaseHelperInput, KeepsAForwardSlashRootAndTrimsNonrootTrailingSeparators)
{
	EXPECT_EQ(QuoteDirectoryArgument(L"C:/"), L"\"C:/\"");
	EXPECT_EQ(QuoteDirectoryArgument(L"C:\\Music\\\\"), L"\"C:\\Music\"");
}
