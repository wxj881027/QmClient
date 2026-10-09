// 真实字形排版与控制台按钮布局协作；GPU 设备边界由 fixture 隔离。
#include <game/client/QmUi/QmConsoleUi.h>

#include <test/support/qm_real_ui_fixture.h>

#include <cmath>

namespace
{
	class QmConsoleButtonTextLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		CTextCursor Draw(const QmConsoleUi::SButtonLabelLayout &Layout, const char *pText, float FontSize)
		{
			const QmConsoleUi::CButtonTextStyle TextStyle(*m_Ui.TextRender());
			CTextCursor Cursor;
			Cursor.SetPosition(Layout.m_Position);
			Cursor.m_FontSize = FontSize;
			Cursor.m_LineWidth = Layout.m_LineWidth;
			Cursor.m_Flags |= Layout.m_Flags;
			Cursor.m_CalculateVisualBoundingBox = true;
			m_Ui.TextRender()->TextEx(&Cursor, pText);
			return Cursor;
		}
	};
}

TEST_F(QmConsoleButtonTextLayout, ChineseAndLatinLabelsCenterTheirInkAndKeepTheLastCharacter)
{
	const CUIRect Rect{15.3f, 30.7f, 150, 28};
	for(const char *pText : {"导出截图", "日志级别", "LastZ", "Ágj", " 中文末字 "})
	{
		SCOPED_TRACE(pText);
		const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), Rect, pText, 13.37f, 0);
		const auto Cursor = Draw(Layout, pText, 13.37f);
		ASSERT_TRUE(Cursor.m_HasVisualBoundingBox);
		EXPECT_FALSE(Cursor.m_Truncated);
		EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
		EXPECT_NEAR((Cursor.m_VisualLeft + Cursor.m_VisualRight) * 0.5f, Rect.x + Rect.w * 0.5f, 0.001f);
		EXPECT_NEAR((Cursor.m_VisualTop + Cursor.m_VisualBottom) * 0.5f, Rect.y + Rect.h * 0.5f, 0.001f);
	}
}

TEST_F(QmConsoleButtonTextLayout, IconAndChineseInkCenterAsOneGroup)
{
	const CUIRect Rect{10, 20, 160, 28};
	const char *pText = "隐藏日志";
	const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), Rect, pText, 14, 16);
	const auto Cursor = Draw(Layout, pText, 14);
	ASSERT_TRUE(Cursor.m_HasVisualBoundingBox);
	EXPECT_GT(Layout.m_Content.m_Icon.w, 0);
	EXPECT_GT(Cursor.m_VisualLeft, Layout.m_Content.m_Icon.x + Layout.m_Content.m_Icon.w);
	EXPECT_NEAR((Layout.m_Content.m_Icon.x + Cursor.m_VisualRight) * 0.5f, Rect.x + Rect.w * 0.5f, 0.001f);
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
}

TEST_F(QmConsoleButtonTextLayout, LongTranslationsEllipsizeWithoutChangingTheRequestedFont)
{
	const CUIRect Rect{10, 20, 80, 28};
	const char *pText = "很长的中文按钮文字保留省略标记";
	const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), Rect, pText, 14, 16);
	const auto Cursor = Draw(Layout, pText, 14);
	ASSERT_TRUE(Cursor.m_HasVisualBoundingBox);
	EXPECT_TRUE(Cursor.m_Truncated);
	EXPECT_FLOAT_EQ(Cursor.m_FontSize, 14);
	EXPECT_GE(Cursor.m_VisualLeft, Rect.x);
	EXPECT_LE(Cursor.m_VisualRight, Rect.x + Rect.w);
	EXPECT_NEAR((Layout.m_Content.m_Icon.x + Cursor.m_VisualRight) * 0.5f, Rect.x + Rect.w * 0.5f, 0.001f);
}

TEST_F(QmConsoleButtonTextLayout, OnePointFontUsesTheRequestedSizeAtFractionalPixelScale)
{
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	pGraphics->SetFramebufferSize(1280, 720);
	pGraphics->MapScreen(0, 0, 1066.6667f, 600);
	const CUIRect Rect{10.3f, 20.7f, 120, 20};
	const char *pText = "截图";
	const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), Rect, pText, 1, 0);
	const auto Cursor = Draw(Layout, pText, 1);
	EXPECT_FLOAT_EQ(Cursor.m_FontSize, 1);
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	ASSERT_TRUE(Cursor.m_HasVisualBoundingBox);
	EXPECT_NEAR((Cursor.m_VisualLeft + Cursor.m_VisualRight) * 0.5f, Rect.x + Rect.w * 0.5f, 0.001f);
}

TEST_F(QmConsoleButtonTextLayout, MeasuringOrdinaryLabelsRestoresIconPresetAndCallerFlags)
{
	const unsigned Flags = TEXT_RENDER_FLAG_NO_X_BEARING | TEXT_RENDER_FLAG_NO_Y_BEARING | TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH;
	const unsigned PreviousFlags = m_Ui.TextRender()->GetRenderFlags();
	const EFontPreset PreviousPreset = m_Ui.TextRender()->GetFontPreset();
	m_Ui.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	m_Ui.TextRender()->SetRenderFlags(Flags);
	const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), {0, 0, 120, 24}, "截图", 12, 0);
	EXPECT_GT(Layout.m_VisualWidth, 0);
	EXPECT_EQ(m_Ui.TextRender()->GetRenderFlags(), Flags);
	EXPECT_EQ(m_Ui.TextRender()->GetFontPreset(), EFontPreset::ICON_FONT);
	m_Ui.TextRender()->SetRenderFlags(PreviousFlags);
	m_Ui.TextRender()->SetFontPreset(PreviousPreset);
}

TEST_F(QmConsoleButtonTextLayout, SubGlyphWidthAndEmptyButtonsReserveNoLabelSlot)
{
	for(float Width : {0.0f, 1.0f, 3.0f})
	{
		SCOPED_TRACE(Width);
		const CUIRect Rect{10, 20, Width, 28};
		const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), Rect, "导出截图", 14, 0);
		EXPECT_FLOAT_EQ(Layout.m_Content.m_Label.w, 0);
	}
}

TEST_F(QmConsoleButtonTextLayout, EmptyLabelsKeepOnlyTheIconSlot)
{
	const CUIRect Rect{10, 20, 120, 28};
	const auto Layout = QmConsoleUi::MeasureButtonLabel(*m_Ui.TextRender(), Rect, "", 14, 16);
	EXPECT_FLOAT_EQ(Layout.m_Content.m_Label.w, 0);
	EXPECT_NEAR(Layout.m_Content.m_Icon.x + Layout.m_Content.m_Icon.w * 0.5f, Rect.x + Rect.w * 0.5f, 0.001f);
}

TEST_F(QmConsoleButtonTextLayout, OnePointFontAtLowPixelRatioKeepsNonzeroLayoutHeight)
{
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	pGraphics->SetFramebufferSize(1280, 720);
	for(float Height : {1200.0f, 2400.0f})
	{
		SCOPED_TRACE(Height);
		pGraphics->MapScreen(0, 0, Height * 1280.0f / 720.0f, Height);
		const float FontSize = QmConsoleAppearance::FontSize(1);
		auto Bounds = m_Ui.TextRender()->TextBoundingBox(FontSize, "中文 LastZ", -1, 300, 1.0f, 0);
		EXPECT_GT(Bounds.m_H, 0.0f);
		EXPECT_TRUE(std::isfinite(Bounds.m_H));
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = 300;
		Cursor.m_LineSpacing = 1;
		Cursor.m_Flags = 0;
		m_Ui.TextRender()->TextEx(&Cursor, "中文 LastZ");
		EXPECT_GE(Cursor.m_AlignedFontSize * 720.0f / Height, 1.0f);
		EXPECT_GT(Cursor.Height(), 0.0f);
		EXPECT_EQ(Cursor.m_CharCount, str_length("中文 LastZ"));
		EXPECT_FLOAT_EQ(Bounds.m_H, Cursor.Height());
	}
}

TEST_F(QmConsoleButtonTextLayout, ConsecutiveSelectedLinesCoverTheWholeLineCell)
{
	QmConsoleUi::CButtonTextStyle Style(*m_Ui.TextRender());
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(20, 30));
	Cursor.m_FontSize = 12;
	Cursor.m_LineSpacing = 1;
	Cursor.m_LineWidth = 300;
	Cursor.m_MaxLines = 4;
	Cursor.m_CalculateSelectionMode = TEXT_CURSOR_SELECTION_MODE_SET;
	Cursor.m_SelectionStart = 0;
	Cursor.m_SelectionEnd = 12;
	Cursor.m_RenderSelection = false;
	Cursor.m_SelectionHeightFactor = 1;
	m_Ui.TextRender()->TextEx(&Cursor, "firstZ\nsecond");
	ASSERT_GE(Cursor.m_vSelectionQuads.size(), 2u);
	const auto First = QmConsoleUi::LayoutSelectionBackground(Cursor.m_vSelectionQuads[0], 12);
	const auto Second = QmConsoleUi::LayoutSelectionBackground(Cursor.m_vSelectionQuads[1], 12);
	EXPECT_NEAR(First.y + First.h, Second.y, 0.001f);
}
