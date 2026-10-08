#include <game/client/components/qmclient/qm_chat_text_layout.h>

#include <gtest/gtest.h>

TEST(QmChatTextLayout, OriginalAndTranslationKeepTheirOwnWrappedFontHeights)
{
	CTextCursor Original;
	Original.m_FontSize = 10.0f;
	Original.m_LineWidth = 120.0f;
	Original.m_LineSpacing = 2.0f;
	bool OriginalMeasured = false;
	const auto Layout = QmChatMeasureTextBlocks(Original, 8.0f, [&](CTextCursor &Cursor) {
			EXPECT_FLOAT_EQ(Cursor.m_FontSize, 10.0f);
			Cursor.m_AlignedFontSize = 10.0f;
			Cursor.m_AlignedLineSpacing = 2.0f;
			Cursor.m_LineCount = 3;
			Cursor.m_LongestLineWidth = 100.0f;
			OriginalMeasured = true; }, [&](CTextCursor &Cursor) {
			EXPECT_TRUE(OriginalMeasured);
			EXPECT_FLOAT_EQ(Cursor.m_FontSize, 8.0f);
			EXPECT_EQ(Cursor.m_LineCount, 1);
			EXPECT_FLOAT_EQ(Cursor.m_LineWidth, 120.0f);
			Cursor.m_AlignedFontSize = 8.0f;
			Cursor.m_AlignedLineSpacing = 2.0f;
			Cursor.m_LineCount = 4;
			Cursor.m_LongestLineWidth = 115.0f; });
	EXPECT_FLOAT_EQ(Layout.m_SecondaryOffset, 36.0f);
	EXPECT_FLOAT_EQ(Layout.m_Height, 76.0f);
	EXPECT_FLOAT_EQ(Layout.m_Width, 115.0f);
	EXPECT_FLOAT_EQ(Original.m_FontSize, 10.0f);
}

TEST(QmChatTextLayout, VisualBearingsSeparateBlocksAndReserveDescenders)
{
	CTextCursor Original;
	const auto Layout = QmChatMeasureTextBlocks(Original, 8.0f, [](CTextCursor &Cursor) {
			Cursor.m_AlignedFontSize = 10.0f;
			Cursor.m_LineCount = 2;
			Cursor.m_HasVisualBoundingBox = true;
			Cursor.m_VisualTop = -3.0f;
			Cursor.m_VisualBottom = 25.0f; }, [](CTextCursor &Cursor) {
			Cursor.m_AlignedFontSize = 8.0f;
			Cursor.m_HasVisualBoundingBox = true;
			Cursor.m_VisualTop = -2.0f;
			Cursor.m_VisualBottom = 10.0f; });
	EXPECT_FLOAT_EQ(Layout.m_SecondaryOffset, 27.0f);
	EXPECT_FLOAT_EQ(Layout.m_PrimaryOffset, 3.0f);
	EXPECT_FLOAT_EQ(Layout.m_Height, 40.0f);
	CTextCursor RenderedOriginal;
	RenderedOriginal.SetPosition(vec2(20.0f, 100.0f + Layout.m_PrimaryOffset));
	const auto RenderedSecondary = QmChatSecondaryCursor(RenderedOriginal, 8.0f, Layout.m_SecondaryOffset);
	EXPECT_FLOAT_EQ(RenderedSecondary.m_StartY, 130.0f);
	EXPECT_FLOAT_EQ(RenderedSecondary.m_StartY - 2.0f, 128.0f);
	EXPECT_FLOAT_EQ(100.0f + Layout.m_Height, 140.0f);
}

TEST(QmChatTextLayout, PrefixWrappingRemainsInOriginalBlock)
{
	CTextCursor Original;
	Original.m_LineCount = 3;
	Original.m_FontSize = 12.0f;
	Original.m_AlignedFontSize = 12.0f;
	Original.m_X = 45.0f;
	Original.m_StartX = 30.0f;
	Original.m_LineWidth = 80.0f;
	const auto Layout = QmChatMeasureTextBlocks(Original, 6.0f, [](CTextCursor &Cursor) { ++Cursor.m_LineCount; }, [](CTextCursor &Cursor) {
			EXPECT_EQ(Cursor.m_LineCount, 1);
			EXPECT_FLOAT_EQ(Cursor.m_X, 30.0f);
			Cursor.m_AlignedFontSize = 6.0f; });
	EXPECT_FLOAT_EQ(Layout.m_SecondaryOffset, 48.0f);
	EXPECT_FLOAT_EQ(Layout.m_Height, 54.0f);
}

TEST(QmChatTextLayout, SecondaryKeepsContainerCharacterOffsetsAndSweepTracking)
{
	CTextCursor Original;
	Original.m_CharCount = 31;
	Original.m_GlyphCount = 17;
	Original.m_TrackLineRanges = true;
	Original.m_Flags = 0;
	Original.m_vColorSplits.emplace_back(0, 1, ColorRGBA(1, 0, 0, 1));
	const auto Secondary = QmChatSecondaryCursor(Original, 8.0f, 20.0f);
	EXPECT_EQ(Secondary.m_CharCount, 31);
	EXPECT_EQ(Secondary.m_GlyphCount, 17);
	EXPECT_TRUE(Secondary.m_TrackLineRanges);
	EXPECT_TRUE(Secondary.m_CalculateVisualBoundingBox);
	EXPECT_EQ(Secondary.m_Flags, 0);
	EXPECT_TRUE(Secondary.m_vColorSplits.empty());
}

TEST(QmChatTextLayout, TranslationSizeDefaultsAndConfigurationLimits)
{
	EXPECT_FLOAT_EQ(QmChatTranslationFontSize(10.0f, 80), 8.0f);
	EXPECT_FLOAT_EQ(QmChatTranslationFontSize(10.0f, 50), 5.0f);
	EXPECT_FLOAT_EQ(QmChatTranslationFontSize(10.0f, 100), 10.0f);
	EXPECT_FLOAT_EQ(QmChatTranslationFontSize(10.0f, -10), 5.0f);
	EXPECT_FLOAT_EQ(QmChatTranslationFontSize(10.0f, 300), 10.0f);
}

TEST(QmChatTextLayout, UntranslatedHeightUsesOnlyItsOwnLineMetrics)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(5.0f, 100.0f));
	Cursor.m_LineCount = 2;
	Cursor.m_AlignedFontSize = 10.0f;
	Cursor.m_AlignedLineSpacing = 1.0f;
	EXPECT_FLOAT_EQ(QmChatTextBlockBottom(Cursor), 22.0f);
	Cursor.m_HasVisualBoundingBox = true;
	Cursor.m_VisualBottom = 126.0f;
	EXPECT_FLOAT_EQ(QmChatTextBlockBottom(Cursor), 26.0f);
}

TEST(QmChatTextLayout, LongPrefixRetainsHalfWidthForWrappedBody)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(20.0f, 50.0f));
	Cursor.m_X = 219.0f;
	Cursor.m_LineWidth = 200.0f;
	Cursor.m_LongestLineWidth = 199.0f;
	QmChatApplyMessageIndent(Cursor);
	EXPECT_FLOAT_EQ(Cursor.m_StartX, 120.0f);
	EXPECT_FLOAT_EQ(Cursor.m_LineWidth, 100.0f);
	EXPECT_FLOAT_EQ(Cursor.m_StartX + Cursor.m_LineWidth, 220.0f);
	EXPECT_FLOAT_EQ(Cursor.m_X, 219.0f);
}

TEST(QmChatTextLayout, WrappedPrefixUsesCurrentLineRatherThanLongestLine)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(20.0f, 50.0f));
	Cursor.m_X = 50.0f;
	Cursor.m_Y = 62.0f;
	Cursor.m_LineWidth = 200.0f;
	Cursor.m_LongestLineWidth = 198.0f;
	Cursor.m_LineCount = 2;
	Cursor.m_CharCount = 33;
	QmChatApplyMessageIndent(Cursor);
	EXPECT_FLOAT_EQ(Cursor.m_StartX, 50.0f);
	EXPECT_FLOAT_EQ(Cursor.m_LineWidth, 170.0f);
	EXPECT_FLOAT_EQ(Cursor.m_Y, 62.0f);
	EXPECT_EQ(Cursor.m_LineCount, 2);
	EXPECT_EQ(Cursor.m_CharCount, 33);
}

TEST(QmChatTextLayout, ShortPrefixPreservesExistingIndent)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(5.0f, 50.0f));
	Cursor.m_X = 35.0f;
	Cursor.m_LineWidth = 200.0f;
	QmChatApplyMessageIndent(Cursor);
	EXPECT_FLOAT_EQ(Cursor.m_StartX, 35.0f);
	EXPECT_FLOAT_EQ(Cursor.m_LineWidth, 170.0f);
}

TEST(QmChatTextLayout, PrefixEndingWithNewlineLeavesFullBodyWidth)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(5.0f, 50.0f));
	Cursor.m_LineWidth = 200.0f;
	Cursor.m_LongestLineWidth = 195.0f;
	QmChatApplyMessageIndent(Cursor);
	EXPECT_FLOAT_EQ(Cursor.m_StartX, 5.0f);
	EXPECT_FLOAT_EQ(Cursor.m_LineWidth, 200.0f);
}

TEST(QmChatTextLayout, UnboundedLineRemainsUnbounded)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(5.0f, 50.0f));
	Cursor.m_X = 35.0f;
	QmChatApplyMessageIndent(Cursor);
	EXPECT_FLOAT_EQ(Cursor.m_StartX, 5.0f);
	EXPECT_FLOAT_EQ(Cursor.m_LineWidth, -1.0f);
}

TEST(QmChatTextLayout, LongPrefixBackgroundFitsActualTextEdges)
{
	CTextCursor Prefix;
	Prefix.SetPosition(vec2(20.0f, 50.0f));
	Prefix.m_X = 219.0f;
	Prefix.m_LineWidth = 200.0f;
	Prefix.m_LongestLineWidth = 199.0f;
	CTextCursor Body = Prefix;
	QmChatApplyMessageIndent(Body);
	Body.m_LongestLineWidth = 100.0f;
	EXPECT_FLOAT_EQ(QmChatIndentedContentWidth(Prefix, Body), 200.0f);
}

TEST(QmChatTextLayout, ShortPrefixBackgroundIncludesIndentedBody)
{
	CTextCursor Prefix;
	Prefix.SetPosition(vec2(20.0f, 50.0f));
	Prefix.m_X = 50.0f;
	Prefix.m_LineWidth = 200.0f;
	Prefix.m_LongestLineWidth = 30.0f;
	CTextCursor Body = Prefix;
	QmChatApplyMessageIndent(Body);
	Body.m_LongestLineWidth = 90.0f;
	EXPECT_FLOAT_EQ(QmChatIndentedContentWidth(Prefix, Body), 120.0f);
}
