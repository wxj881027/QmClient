// 浏览器筛选按钮与表格标签的真实字形排版；仅隔离 GPU 设备提交。
#include <game/client/QmUi/BrowserTableLabel.h>
#include <game/client/QmUi/ServerBrowserFilterLayout.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmServerBrowserFilterLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUIElement::SUIElementRect m_Label;

		void TearDown() override
		{
			m_Ui.TextRender()->DeleteTextContainer(m_Label.m_UITextContainer);
			CRealUiFixture::TearDown();
		}

		CUIRect DrawStrictButtonLabel(const char *pText, bool Streamed)
		{
			SSettingsContentMetrics Metrics;
			Metrics.m_LineHeight = 18.0f;
			Metrics.m_BodySize = 14.0f * CUi::ms_FontmodHeight;
			Metrics.m_LineSpacing = 3.0f;
			const auto Size = MeasureServerBrowserFilterStrictButton(*m_Ui.TextRender(), pText, Metrics);
			const CUIRect Button{108.37f, 120.19f, Size.m_Width, Size.m_Height};
			const CUIRect Label = ResolveSettingsToggleButtonLabel(Button, Metrics.m_BodySize * 0.5f, Metrics.m_LineSpacing * 0.5f, Metrics.m_BodySize + Metrics.m_LineSpacing);
			SLabelProperties Props;
			Props.m_MaxWidth = Label.w;
			Props.m_EnableWidthCheck = false;
			Props.m_MinimumFontSize = Metrics.m_BodySize;
			m_Ui.TextRender()->DeleteTextContainer(m_Label.m_UITextContainer);
			if(Streamed)
				m_Ui.DoLabelStreamed(m_Label, &Label, pText, Metrics.m_BodySize, TEXTALIGN_MC, Props);
			else
				m_Ui.DoLabel(m_Label, &Label, pText, Metrics.m_BodySize, TEXTALIGN_MC, Props);
			return Button;
		}
	};

	class QmBrowserTableLabel : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUIElement::SUIElementRect m_Label;

		void TearDown() override
		{
			m_Ui.TextRender()->DeleteTextContainer(m_Label.m_UITextContainer);
			CRealUiFixture::TearDown();
		}
	};
}

TEST_F(QmServerBrowserFilterLayout, TightChineseLabelKeepsAllFourGlyphsOnOneLine)
{
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	for(const int Height : {720, 900, 1080})
	{
		SCOPED_TRACE(Height);
		pGraphics->SetFramebufferSize(Height * 16 / 9, Height);
		pGraphics->MapScreen(0, 0, 600.0f * 16.0f / 9.0f, 600);
		for(const bool Streamed : {false, true})
		{
			SCOPED_TRACE(Streamed);
			const char *pText = "严格匹配";
			const CUIRect Button = DrawStrictButtonLabel(pText, Streamed);
			ASSERT_TRUE(m_Label.m_UITextContainer.Valid());
			EXPECT_EQ(m_Label.m_Cursor.m_LineCount, 1);
			EXPECT_EQ(m_Label.m_Cursor.m_GlyphCount, 4);
			EXPECT_EQ(m_Label.m_Cursor.m_CharCount, str_length(pText));
			EXPECT_FALSE(m_Label.m_Cursor.m_Truncated);
			EXPECT_LE(m_Label.m_Cursor.Height(), Button.h);
		}
	}
}

TEST_F(QmServerBrowserFilterLayout, MultilineTranslationReservesItsActualHeight)
{
	const char *pText = "严格\n匹配";
	const CUIRect Button = DrawStrictButtonLabel(pText, true);
	ASSERT_TRUE(m_Label.m_UITextContainer.Valid());
	EXPECT_EQ(m_Label.m_Cursor.m_LineCount, 2);
	EXPECT_EQ(m_Label.m_Cursor.m_CharCount, str_length(pText));
	EXPECT_FALSE(m_Label.m_Cursor.m_Truncated);
	EXPECT_GE(Button.h, m_Label.m_Cursor.Height() + 3.0f);
}

TEST_F(QmBrowserTableLabel, NarrowColumnsEllipsizeWithoutReducingFontSize)
{
	for(const float Width : {20.0f, 40.0f, 80.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Rect{0, 0, Width, 24};
		const auto Props = QmBrowserTableLabelProperties(Rect.w);
		m_Ui.DoLabelStreamed(m_Label, &Rect, "DDRaceNetwork - a long server name", 14.0f, TEXTALIGN_ML, Props);
		ASSERT_TRUE(m_Label.m_UITextContainer.Valid());
		EXPECT_FLOAT_EQ(m_Label.m_FontSize, 14.0f);
		EXPECT_FLOAT_EQ(m_Label.m_Cursor.m_FontSize, 14.0f);
		EXPECT_EQ(m_Label.m_Cursor.m_LineCount, 1);
		EXPECT_TRUE(m_Label.m_Cursor.m_Truncated);
		EXPECT_LE(m_Label.m_Cursor.m_LongestLineWidth, Width + 0.01f);
		EXPECT_TRUE(m_Ui.DoLabel(&Rect, "DDRaceNetwork - a long server name", 14.0f, TEXTALIGN_ML, Props).m_Truncated);
	}
}

TEST_F(QmBrowserTableLabel, WideningNumericColumnRestoresFullTextAtSameSize)
{
	CUIRect Rect{0, 0, 20, 24};
	m_Ui.DoLabelStreamed(m_Label, &Rect, "128/128", 14.0f, TEXTALIGN_MR, QmBrowserTableLabelProperties(Rect.w));
	ASSERT_TRUE(m_Label.m_Cursor.m_Truncated);
	EXPECT_FLOAT_EQ(m_Label.m_FontSize, 14.0f);

	Rect.w = 200.0f;
	m_Ui.DoLabelStreamed(m_Label, &Rect, "128/128", 14.0f, TEXTALIGN_MR, QmBrowserTableLabelProperties(Rect.w));
	EXPECT_FLOAT_EQ(m_Label.m_FontSize, 14.0f);
	EXPECT_FLOAT_EQ(m_Label.m_Cursor.m_FontSize, 14.0f);
	EXPECT_FALSE(m_Label.m_Cursor.m_Truncated);
	EXPECT_EQ(m_Label.m_Cursor.m_GlyphCount, 7);
	EXPECT_EQ(m_Label.m_Cursor.m_LineCount, 1);
}

TEST_F(QmBrowserTableLabel, TruncationWithoutEllipsisAlsoPreservesFontSize)
{
	const CUIRect Rect{0, 0, 20, 24};
	m_Ui.DoLabelStreamed(m_Label, &Rect, "123456789", 9.0f, TEXTALIGN_MC, QmBrowserTableLabelProperties(Rect.w, false));
	ASSERT_TRUE(m_Label.m_UITextContainer.Valid());
	EXPECT_FLOAT_EQ(m_Label.m_FontSize, 9.0f);
	EXPECT_FLOAT_EQ(m_Label.m_Cursor.m_FontSize, 9.0f);
	EXPECT_TRUE(m_Label.m_Cursor.m_Truncated);
	EXPECT_LE(m_Label.m_Cursor.m_LongestLineWidth, Rect.w + 0.01f);
}
