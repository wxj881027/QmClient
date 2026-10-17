#include <engine/client/text_utf8_cursor.h>

#include <gtest/gtest.h>

#include <limits>

TEST(TextUtf8Cursor, ReadsAsciiWithoutRequiringATerminator)
{
	const char aText[] = {'A'};
	CQmTextUtf8Cursor Cursor(aText, sizeof(aText));
	EXPECT_EQ(Cursor.Next(), 'A');
	EXPECT_EQ(Cursor.Next(), 0);
	EXPECT_EQ(Cursor.Next(), 0);
}

TEST(TextUtf8Cursor, DecodesCompleteMultibyteCodepointsWithinTheRange)
{
	const char aText[] = "A中😀";
	CQmTextUtf8Cursor Cursor(aText, sizeof(aText) - 1);
	EXPECT_EQ(Cursor.Next(), 'A');
	EXPECT_EQ(Cursor.Next(), 0x4e2d);
	EXPECT_EQ(Cursor.Next(), 0x1f600);
	EXPECT_EQ(Cursor.Next(), 0);
}

TEST(TextUtf8Cursor, ByteLimitDoesNotConsumeTheRestOfAMultibyteCharacter)
{
	const char aText[] = "Aö";
	CQmTextUtf8Cursor Cursor(aText, 2);
	EXPECT_EQ(Cursor.Next(), 'A');
	EXPECT_EQ(Cursor.Next(), -1);
	EXPECT_EQ(Cursor.Next(), 0);
}

TEST(TextUtf8Cursor, TruncatedUnterminatedSequencesStopAtTheAvailableBytes)
{
	const char aOne[] = {static_cast<char>(0xf0)};
	const char aTwo[] = {static_cast<char>(0xf0), static_cast<char>(0x9f)};
	const char aThree[] = {static_cast<char>(0xf0), static_cast<char>(0x9f), static_cast<char>(0x98)};
	CQmTextUtf8Cursor aCursors[] = {{aOne, 1}, {aTwo, 2}, {aThree, 3}};
	for(auto &Cursor : aCursors)
	{
		EXPECT_EQ(Cursor.Next(), -1);
		EXPECT_EQ(Cursor.Next(), 0);
	}
}

TEST(TextUtf8Cursor, InvalidSequencePreservesTheFollowingAsciiCharacter)
{
	const char aText[] = {static_cast<char>(0xc3), 'A'};
	CQmTextUtf8Cursor Cursor(aText, sizeof(aText));
	EXPECT_EQ(Cursor.Next(), -1);
	EXPECT_EQ(Cursor.Next(), 'A');
	EXPECT_EQ(Cursor.Next(), 0);
}

TEST(TextUtf8Cursor, TerminatorEndsEvenWhenTheSuppliedLimitIsLarger)
{
	const char aText[] = {'A', '\0'};
	CQmTextUtf8Cursor Cursor(aText, std::numeric_limits<int>::max());
	EXPECT_EQ(Cursor.Next(), 'A');
	EXPECT_EQ(Cursor.Next(), 0);
}

TEST(TextUtf8Cursor, NullAndNonpositiveRangesAreEmpty)
{
	CQmTextUtf8Cursor Null(nullptr, 1);
	CQmTextUtf8Cursor Empty("a", 0);
	CQmTextUtf8Cursor Negative("a", -1);
	EXPECT_EQ(Null.Next(), 0);
	EXPECT_EQ(Empty.Next(), 0);
	EXPECT_EQ(Negative.Next(), 0);
}
