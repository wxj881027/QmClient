#include <game/client/components/qmclient/colored_parts.h>

#include <gtest/gtest.h>

#include <limits>
#include <utility>

TEST(QmColoredParts, DisabledParsingBorrowsTheUnchangedInput)
{
	const char aInput[] = "before[[red]]after";
	const CColoredParts Text(aInput, false);
	EXPECT_EQ(Text.Text(), aInput);
	EXPECT_STREQ(Text.Text(), "before[[red]]after");
	EXPECT_TRUE(Text.Colors().empty());
}

TEST(QmColoredParts, PlainTextBorrowsTheOriginalBuffer)
{
	const char aInput[] = "plain text";
	const CColoredParts Text(aInput, true);
	EXPECT_EQ(Text.Text(), aInput);
	EXPECT_TRUE(Text.Colors().empty());
}

TEST(QmColoredParts, EmptyTextKeepsTheBorrowedEmptyString)
{
	const char aInput[] = "";
	const CColoredParts Text(aInput, true);
	EXPECT_EQ(Text.Text(), aInput);
	EXPECT_TRUE(Text.Colors().empty());
}

TEST(QmColoredParts, ParsedTextOwnsItsBufferAndRetainsColorOffsets)
{
	char aInput[] = "prefix[[red]]red[[$00FF00]]green";
	const CColoredParts Text(aInput, true);
	ASSERT_NE(Text.Text(), aInput);
	aInput[0] = 'x';
	EXPECT_STREQ(Text.Text(), "prefixredgreen");
	ASSERT_EQ(Text.Colors().size(), 2u);
	EXPECT_EQ(Text.Colors()[0].m_Index, 6);
	EXPECT_EQ(Text.Colors()[1].m_Index, 9);
	EXPECT_FLOAT_EQ(Text.Colors()[0].m_Color.r, 1.0f);
	EXPECT_FLOAT_EQ(Text.Colors()[0].m_Color.g, 0.0f);
	EXPECT_FLOAT_EQ(Text.Colors()[1].m_Color.g, 1.0f);
}

TEST(QmColoredParts, UnclosedMarkerKeepsTheRemainingText)
{
	const CColoredParts Text("before[[red", true);
	EXPECT_STREQ(Text.Text(), "before[[red");
	EXPECT_TRUE(Text.Colors().empty());
}

TEST(QmColoredParts, InvalidClosedMarkerKeepsTheExistingRemovalRule)
{
	const CColoredParts Text("before[[not-a-color]]after", true);
	EXPECT_STREQ(Text.Text(), "beforeafter");
	EXPECT_TRUE(Text.Colors().empty());
}

TEST(QmColoredParts, MovingOwnedTextPreservesItsBufferAndColorEntries)
{
	CColoredParts Source("[[red]]owned", true);
	const char *pOwned = Source.Text();
	CColoredParts Target(std::move(Source));
	EXPECT_EQ(Target.Text(), pOwned);
	EXPECT_STREQ(Target.Text(), "owned");
	ASSERT_EQ(Target.Colors().size(), 1u);
	EXPECT_EQ(Target.Colors()[0].m_Index, 0);
	EXPECT_EQ(Source.Text(), nullptr);
}

TEST(QmColoredParts, MovingBorrowedTextRetainsTheExternalPointer)
{
	const char aInput[] = "borrowed";
	CColoredParts Source(aInput, true);
	CColoredParts Target(std::move(Source));
	EXPECT_EQ(Target.Text(), aInput);
	EXPECT_TRUE(Target.Colors().empty());
	EXPECT_EQ(Source.Text(), nullptr);
}

TEST(QmColoredParts, MoveAssignmentReplacesAnOwnedTargetWithOwnedText)
{
	CColoredParts Target("[[blue]]old", true);
	CColoredParts Source("[[red]]new", true);
	const char *pSource = Source.Text();
	Target = std::move(Source);
	EXPECT_EQ(Target.Text(), pSource);
	EXPECT_STREQ(Target.Text(), "new");
	ASSERT_EQ(Target.Colors().size(), 1u);
	EXPECT_FLOAT_EQ(Target.Colors()[0].m_Color.r, 1.0f);
	EXPECT_EQ(Source.Text(), nullptr);
}

TEST(QmColoredParts, MoveAssignmentCanReplaceOwnedTextWithBorrowedText)
{
	const char aInput[] = "borrowed";
	CColoredParts Target("[[red]]owned", true);
	CColoredParts Source(aInput, true);
	Target = std::move(Source);
	EXPECT_EQ(Target.Text(), aInput);
	EXPECT_TRUE(Target.Colors().empty());
	EXPECT_EQ(Source.Text(), nullptr);
}

TEST(QmColoredParts, MoveAssignmentCanReplaceBorrowedTextWithOwnedText)
{
	const char aInput[] = "borrowed";
	CColoredParts Target(aInput, true);
	CColoredParts Source("[[red]]owned", true);
	const char *pSource = Source.Text();
	Target = std::move(Source);
	EXPECT_EQ(Target.Text(), pSource);
	EXPECT_STREQ(Target.Text(), "owned");
	EXPECT_EQ(Source.Text(), nullptr);
}

TEST(QmColoredParts, SelfMoveAssignmentKeepsTheOriginalText)
{
	CColoredParts Text("[[red]]same", true);
	const char *pOwned = Text.Text();
	CColoredParts &Alias = Text;
	Text = std::move(Alias);
	EXPECT_EQ(Text.Text(), pOwned);
	EXPECT_STREQ(Text.Text(), "same");
	EXPECT_EQ(Text.Colors().size(), 1u);
}

TEST(QmColoredParts, MovedFromObjectCanReceiveAnotherOwnedText)
{
	CColoredParts Source("[[red]]first", true);
	CColoredParts Target(std::move(Source));
	Source = CColoredParts("[[blue]]second", true);
	EXPECT_STREQ(Source.Text(), "second");
	EXPECT_STREQ(Target.Text(), "first");
}

TEST(QmColoredPreparation, SuccessfulDisplayPreparationKeepsParsedTextAndColors)
{
	const auto Text = CColoredParts::ParseOrBorrow("ab[[red]]cd", true);
	EXPECT_FALSE(Text.ParseFailed());
	EXPECT_STREQ(Text.Text(), "abcd");
	ASSERT_EQ(Text.Colors().size(), 1u);
	EXPECT_EQ(Text.Colors()[0].m_Index, 2);
}

TEST(QmColoredPreparation, DisabledDisplayParsingKeepsTheBorrowedInput)
{
	const char aInput[] = "[[red]]literal";
	const auto Text = CColoredParts::ParseOrBorrow(aInput, false);
	EXPECT_FALSE(Text.ParseFailed());
	EXPECT_EQ(Text.Text(), aInput);
	EXPECT_TRUE(Text.Colors().empty());
}

TEST(QmColoredPreparation, PlainDisplayTextKeepsTheBorrowedInput)
{
	const char aInput[] = "plain";
	const auto Text = CColoredParts::ParseOrBorrow(aInput, true);
	EXPECT_EQ(Text.Text(), aInput);
	EXPECT_FALSE(Text.ParseFailed());
}

TEST(QmColoredPreparation, AppendingSplitsPreservesExistingLayersAndCursorPosition)
{
	const CColoredParts Text("ab[[red]]c[[blue]]d", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = 10;
	Cursor.m_vColorSplits.emplace_back(2, 4, ColorRGBA(0, 1, 0, 1));
	ASSERT_TRUE(Text.TryAddSplitsToCursor(Cursor));
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 3u);
	EXPECT_EQ(Cursor.m_CharCount, 10);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, 2);
	EXPECT_EQ(Cursor.m_vColorSplits[1].m_CharIndex, 12);
	EXPECT_EQ(Cursor.m_vColorSplits[1].m_Length, 1);
	EXPECT_EQ(Cursor.m_vColorSplits[2].m_CharIndex, 13);
	EXPECT_EQ(Cursor.m_vColorSplits[2].m_Length, 9999);
}

TEST(QmColoredPreparation, UnrepresentableSplitStartDoesNotChangeExistingLayers)
{
	const CColoredParts Text("a[[red]]b", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = std::numeric_limits<int>::max();
	Cursor.m_vColorSplits.emplace_back(2, 4, ColorRGBA(0, 1, 0, 1));
	EXPECT_FALSE(Text.TryAddSplitsToCursor(Cursor));
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, 2);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Length, 4);
	EXPECT_EQ(Cursor.m_CharCount, std::numeric_limits<int>::max());
}

TEST(QmColoredPreparation, RendererEndOverflowDoesNotPublishTheCandidateSplit)
{
	const CColoredParts Text("[[red]]x", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = std::numeric_limits<int>::max() - 9998;
	EXPECT_FALSE(Text.TryAddSplitsToCursor(Cursor));
	EXPECT_TRUE(Cursor.m_vColorSplits.empty());
}

TEST(QmColoredPreparation, RendererEndAtTheIntegerLimitRemainsRepresentable)
{
	const CColoredParts Text("[[red]]x", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = std::numeric_limits<int>::max() - 9999;
	ASSERT_TRUE(Text.TryAddSplitsToCursor(Cursor));
	ASSERT_EQ(Cursor.m_vColorSplits.size(), 1u);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_CharIndex, std::numeric_limits<int>::max() - 9999);
	EXPECT_EQ(Cursor.m_vColorSplits[0].m_Length, 9999);
}

TEST(QmColoredPreparation, EmptyDuplicateSplitCannotUnderflowTheRendererEndMinusOne)
{
	const CColoredParts Text("[[red]][[blue]]x", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = std::numeric_limits<int>::min();
	EXPECT_FALSE(Text.TryAddSplitsToCursor(Cursor));
	EXPECT_TRUE(Cursor.m_vColorSplits.empty());
}

TEST(QmColoredPreparation, TextWithoutColorsDoesNotRejectAnExtremeCursor)
{
	const CColoredParts Text("plain", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = std::numeric_limits<int>::max();
	EXPECT_TRUE(Text.TryAddSplitsToCursor(Cursor));
	EXPECT_TRUE(Cursor.m_vColorSplits.empty());
}

TEST(QmColoredPreparation, ThrowingSplitInterfaceRejectsTheWholeInvalidBatch)
{
	const CColoredParts Text("[[red]][[blue]]x", true);
	CTextCursor Cursor;
	Cursor.m_CharCount = std::numeric_limits<int>::min();
	EXPECT_THROW(Text.AddSplitsToCursor(Cursor), std::length_error);
	EXPECT_TRUE(Cursor.m_vColorSplits.empty());
}
