#include <test/support/qm_real_ui_fixture.h>

class QmTextLayoutBounds : public qm_ui_test::CRealUiFixture
{
};

TEST_F(QmTextLayoutBounds, ByteLimitKeepsTheFollowingUtf8BytesOutOfLayout)
{
	auto *pText = m_Ui.TextRender();
	const char aText[] = "Aö tail";
	const char aPrefix[] = {'A', aText[1], '\0'};
	for(const float LineWidth : {-1.0f, 100.0f})
	{
		SCOPED_TRACE(LineWidth);
		CTextCursor Prefix;
		Prefix.m_Flags = 0;
		Prefix.m_FontSize = 16.0f;
		Prefix.m_LineWidth = LineWidth;
		CTextCursor Bounded = Prefix;

		pText->TextEx(&Prefix, aPrefix);
		pText->TextEx(&Bounded, aText, sizeof(aPrefix) - 1);

		EXPECT_EQ(Bounded.m_CharCount, Prefix.m_CharCount);
		EXPECT_EQ(Bounded.m_GlyphCount, Prefix.m_GlyphCount);
		EXPECT_FLOAT_EQ(Bounded.m_LongestLineWidth, Prefix.m_LongestLineWidth);
	}
}

TEST_F(QmTextLayoutBounds, BoundedPrefixStillWrapsBetweenWords)
{
	auto *pText = m_Ui.TextRender();
	constexpr float FontSize = 16.0f;
	const float WordWidth = pText->TextWidth(FontSize, "word ", -1, 200.0f, TEXTFLAG_STOP_AT_END);
	const float BothWidth = pText->TextWidth(FontSize, "word word", -1, 200.0f, TEXTFLAG_STOP_AT_END);
	ASSERT_GT(BothWidth, WordWidth);
	const float LineWidth = (WordWidth + BothWidth) * 0.5f;

	const char aPrefix[] = "word word";
	const auto Expected = pText->TextBoundingBox(FontSize, aPrefix, -1, LineWidth);
	const auto Bounded = pText->TextBoundingBox(FontSize, "word word tail", sizeof(aPrefix) - 1, LineWidth);

	ASSERT_EQ(Expected.m_LineCount, 2);
	EXPECT_EQ(Bounded.m_LineCount, Expected.m_LineCount);
	EXPECT_FLOAT_EQ(Bounded.m_W, Expected.m_W);
}
