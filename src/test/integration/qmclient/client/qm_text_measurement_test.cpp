#include <test/support/qm_real_ui_fixture.h>

#include <limits>

class QmTextMeasurement : public qm_ui_test::CRealUiFixture
{
};

TEST_F(QmTextMeasurement, WidthStopsAtANullBeforeTheByteLimit)
{
	const char aText[] = {'A', '\0', 'B', '\0'};
	auto *pText = m_Ui.TextRender();
	const int PrefixWidth = pText->CalculateTextWidth("A", 1, 0, 16);
	ASSERT_GT(PrefixWidth, 0);
	EXPECT_EQ(pText->CalculateTextWidth(aText, 3, 0, 16), PrefixWidth);
	EXPECT_EQ(pText->CalculateTextWidth("", 32, 0, 16), 0);
}

TEST_F(QmTextMeasurement, WidthIgnoresATruncatedMultibyteSuffix)
{
	auto *pText = m_Ui.TextRender();
	const int PrefixWidth = pText->CalculateTextWidth("A", 1, 0, 16);
	ASSERT_GT(PrefixWidth, 0);
	EXPECT_EQ(pText->CalculateTextWidth("Aö", 2, 0, 16), PrefixWidth);
	EXPECT_GT(pText->CalculateTextWidth("Aö", 3, 0, 16), PrefixWidth);
}

TEST_F(QmTextMeasurement, InvalidSequenceDoesNotHideFollowingText)
{
	const char aText[] = {static_cast<char>(0xc3), 'A'};
	auto *pText = m_Ui.TextRender();
	EXPECT_EQ(pText->CalculateTextWidth(aText, sizeof(aText), 0, 16), pText->CalculateTextWidth("A", 1, 0, 16));
}

TEST_F(QmTextMeasurement, InvalidInputAndSizesReturnZero)
{
	auto *pText = m_Ui.TextRender();
	EXPECT_EQ(pText->CalculateTextWidth(nullptr, 2, 0, 16), 0);
	EXPECT_EQ(pText->CalculateTextWidth("A", 0, 0, 16), 0);
	EXPECT_EQ(pText->CalculateTextWidth("A", -1, 0, 16), 0);
	EXPECT_EQ(pText->CalculateTextWidth("A", 1, -1, 16), 0);
	EXPECT_EQ(pText->CalculateTextWidth("A", 1, 0, 0), 0);
	EXPECT_EQ(pText->CalculateTextWidth("A", 1, 0, -1), 0);
}

TEST_F(QmTextMeasurement, EmptyTextOrUnavailableWidthProducesNoFittedSize)
{
	auto *pText = m_Ui.TextRender();
	EXPECT_EQ(pText->AdjustFontSize("", 16, 64, 64), 0);
	EXPECT_EQ(pText->AdjustFontSize(nullptr, 16, 64, 64), 0);
	EXPECT_EQ(pText->AdjustFontSize("ö", 1, 64, 64), 0);
	EXPECT_EQ(pText->AdjustFontSize("1", 1, 64, 0), 0);
	EXPECT_EQ(pText->AdjustFontSize("1", 1, 64, -1), 0);
}

TEST_F(QmTextMeasurement, OversizedFitClampsBeforeConvertingToInteger)
{
	auto *pText = m_Ui.TextRender();
	pText->SetCustomFace("DejaVu Sans");
	const int Width = pText->CalculateTextWidth("A", 1, 0, 100);
	ASSERT_GT(Width, 0);
	ASSERT_LT(Width, 100);
	EXPECT_EQ(pText->AdjustFontSize("A", 1, 64, std::numeric_limits<int>::max()), 64);
	EXPECT_EQ(pText->AdjustFontSize("A", 1, 0, std::numeric_limits<int>::max()), std::numeric_limits<int>::max());
}

TEST_F(QmTextMeasurement, DigitOverlayMeasurementRemainsAdditiveAtTheFittedSize)
{
	auto *pText = m_Ui.TextRender();
	const int FontSize = pText->AdjustFontSize("100", 3, 64, 64);
	ASSERT_GT(FontSize, 0);
	ASSERT_LE(FontSize, 64);
	EXPECT_EQ(pText->CalculateTextWidth("100", 3, 0, FontSize),
		pText->CalculateTextWidth("1", 1, 0, FontSize) + 2 * pText->CalculateTextWidth("0", 1, 0, FontSize));
}

TEST_F(QmTextMeasurement, SingleByteGlyphOffsetRejectsMultibyteLeadsAndInvalidSizes)
{
	auto *pText = m_Ui.TextRender();
	EXPECT_EQ(pText->GetGlyphOffsetX(16, static_cast<char>(0xc3)), -1.0f);
	EXPECT_EQ(pText->GetGlyphOffsetX(16, static_cast<char>(0xff)), -1.0f);
	EXPECT_EQ(pText->GetGlyphOffsetX(0, 'A'), -1.0f);
	EXPECT_EQ(pText->GetGlyphOffsetX(-1, 'A'), -1.0f);
	EXPECT_EQ(pText->GetGlyphOffsetX(16, '\0'), 0.0f);
}

TEST_F(QmTextMeasurement, EllipsisKeepsOnlyTheVisibleUtf8PrefixInTheByteCount)
{
	auto *pText = m_Ui.TextRender();
	pText->SetCustomFace("DejaVu Sans");
	constexpr float FontSize = 16.0f;
	constexpr float MeasureWidth = 1024.0f;
	const float PrefixWidth = pText->TextWidth(FontSize, "ää…", -1, MeasureWidth, TEXTFLAG_STOP_AT_END);
	const float WiderPrefix = pText->TextWidth(FontSize, "äää…", -1, MeasureWidth, TEXTFLAG_STOP_AT_END);
	ASSERT_GT(WiderPrefix, PrefixWidth);

	CTextCursor Cursor;
	Cursor.m_Flags = TEXTFLAG_ELLIPSIS_AT_END;
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = (PrefixWidth + WiderPrefix) * 0.5f;
	pText->TextEx(&Cursor, "ääääääää");

	EXPECT_TRUE(Cursor.m_Truncated);
	EXPECT_EQ(Cursor.m_CharCount, 4);
	EXPECT_EQ(Cursor.m_GlyphCount, 3);
	EXPECT_FLOAT_EQ(Cursor.m_LongestLineWidth, PrefixWidth);
}

TEST_F(QmTextMeasurement, EllipsisFitUsesOnlyTheRequestedByteRange)
{
	auto *pText = m_Ui.TextRender();
	pText->SetCustomFace("DejaVu Sans");
	constexpr float FontSize = 16.0f;
	constexpr float MeasureWidth = 1024.0f;
	const float PrefixWidth = pText->TextWidth(FontSize, "iii", -1, MeasureWidth, TEXTFLAG_STOP_AT_END);
	const float EllipsisWidth = pText->TextWidth(FontSize, "i…", -1, MeasureWidth, TEXTFLAG_STOP_AT_END);
	ASSERT_GT(EllipsisWidth, PrefixWidth);

	CTextCursor Cursor;
	Cursor.m_Flags = TEXTFLAG_ELLIPSIS_AT_END;
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = (PrefixWidth + EllipsisWidth) * 0.5f;
	ASSERT_LT(pText->TextWidth(FontSize, "iii"), Cursor.m_LineWidth);
	pText->TextEx(&Cursor, "iiiWWWWWW", 3);

	EXPECT_FALSE(Cursor.m_Truncated);
	EXPECT_EQ(Cursor.m_CharCount, 3);
	EXPECT_EQ(Cursor.m_GlyphCount, 3);
	EXPECT_FLOAT_EQ(Cursor.m_LongestLineWidth, PrefixWidth);
}
