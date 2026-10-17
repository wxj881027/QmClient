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
