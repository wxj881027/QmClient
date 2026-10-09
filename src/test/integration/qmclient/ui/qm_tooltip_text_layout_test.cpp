// 真实文字引擎与共享 Tooltip 布局协作；仅 GPU 设备提交由 fixture 替换。
#include <base/str.h>

#include <game/client/components/tooltips.h>

#include <test/support/qm_real_ui_fixture.h>

#include <array>
#include <string>

namespace
{
	class QmTooltipTextLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		ITextRender &TextRender() { return *m_Ui.TextRender(); }
		SQmTooltipTextLayout Measure(const char *pText, vec2 Available = vec2(500, 300), float Width = 300, float Font = 14)
		{
			return QmTooltipMeasureText(TextRender(), pText, Font, Width, Available, 5);
		}

		// 容器在断言失败时也释放，不让下一项测试继承文字资源。
		struct CContainer
		{
			ITextRender &m_TextRender;
			STextContainerIndex m_Index;
			explicit CContainer(ITextRender &TextRender) : m_TextRender(TextRender) {}
			~CContainer() { m_TextRender.DeleteTextContainer(m_Index); }
		};
	};
}

TEST_F(QmTooltipTextLayout, TightEnglishBubbleKeepsTheLastCharacterOnItsMeasuredLine)
{
	const char *pText = "TooltipLastCharacterZ";
	const auto Layout = Measure(pText);
	ASSERT_FALSE(Layout.m_Truncated);
	ASSERT_EQ(Layout.m_LineCount, 1);
	EXPECT_LT(Layout.m_Size.x - 2 * Layout.m_Padding, Layout.m_LineWidth);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, vec2(13.37f, 27.19f), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	EXPECT_EQ(Cursor.m_GlyphCount, str_length(pText));
	EXPECT_EQ(Cursor.m_LineCount, Layout.m_LineCount);
	EXPECT_FALSE(Cursor.m_Truncated);
	EXPECT_FLOAT_EQ(TextRender().GetBoundingBoxTextContainer(Container.m_Index).m_H, Cursor.Height());
}

TEST_F(QmTooltipTextLayout, TightChineseBubbleKeepsAllTwelveGlyphsIncludingTheLast)
{
	const char *pText = "完整中文提示保留最后一字";
	const auto Layout = Measure(pText);
	ASSERT_FALSE(Layout.m_Truncated);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, vec2(10.31f, 40.71f), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	EXPECT_EQ(Cursor.m_GlyphCount, 12);
	EXPECT_EQ(Cursor.m_LineCount, Layout.m_LineCount);
	EXPECT_FALSE(Cursor.m_Truncated);
}

TEST_F(QmTooltipTextLayout, WrappedAndExplicitLinesKeepTheMeasuredLayoutAndFinalByte)
{
	const char *pText = "第一行保留中文末字\nSecond line wraps without losing its final Z";
	const auto Layout = Measure(pText, vec2(300, 300), 90);
	ASSERT_GT(Layout.m_LineCount, 2);
	ASSERT_FALSE(Layout.m_Truncated);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, vec2(11.1f, 22.2f), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	EXPECT_EQ(Cursor.m_LineCount, Layout.m_LineCount);
	EXPECT_FLOAT_EQ(TextRender().GetBoundingBoxTextContainer(Container.m_Index).m_H, Cursor.Height());
	EXPECT_FALSE(Cursor.m_Truncated);
}

TEST_F(QmTooltipTextLayout, NarrowViewportWrapsWithinAvailableWidthAndRestoresFullText)
{
	const char *pText = "窄视口提示恢复后仍然保留最后一字";
	const auto Narrow = Measure(pText, vec2(70, 60));
	ASSERT_TRUE(Narrow.m_Truncated);
	ASSERT_GT(Narrow.m_VisibleLines, 1);
	EXPECT_LE(Narrow.m_Size.x, 70);
	EXPECT_LE(Narrow.m_Size.y, 60);
	CContainer Prefix(TextRender()), End(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Narrow, pText, vec2(5, 5), Prefix.m_Index);
	EXPECT_LE(Cursor.m_LineCount, Narrow.m_VisibleLines - 1);
	EXPECT_LT(Cursor.m_CharCount, str_length(pText));
	const auto EndCursor = QmTooltipCreateText(TextRender(), Narrow, "…", vec2(5, 5 + (Narrow.m_VisibleLines - 1) * Narrow.m_LineHeight), End.m_Index, true);
	ASSERT_TRUE(End.m_Index.Valid());
	EXPECT_EQ(EndCursor.m_GlyphCount, 1);
	EXPECT_FALSE(EndCursor.m_Truncated);
	const auto Restored = Measure(pText);
	ASSERT_FALSE(Restored.m_Truncated);
	CContainer Full(TextRender());
	const auto FullCursor = QmTooltipCreateText(TextRender(), Restored, pText, vec2(5, 5), Full.m_Index);
	EXPECT_EQ(FullCursor.m_CharCount, str_length(pText));
	EXPECT_EQ(FullCursor.m_LineCount, Restored.m_LineCount);
	EXPECT_FALSE(FullCursor.m_Truncated);
}

TEST_F(QmTooltipTextLayout, OneVisibleLineUsesAnEllipsisInsteadOfClippedBodyText)
{
	const auto Layout = Measure("First line\nSecond line\nThird line", vec2(100, 29));
	ASSERT_EQ(Layout.m_VisibleLines, 1);
	ASSERT_TRUE(Layout.m_Truncated);
	CContainer Body(TextRender()), End(TextRender());
	QmTooltipCreateText(TextRender(), Layout, "First line\nSecond line\nThird line", vec2(5, 5), Body.m_Index);
	EXPECT_FALSE(Body.m_Index.Valid());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, "…", vec2(5, 5), End.m_Index, true);
	ASSERT_TRUE(End.m_Index.Valid());
	EXPECT_EQ(Cursor.m_GlyphCount, 1);
	EXPECT_EQ(Cursor.m_LineCount, 1);
}

TEST_F(QmTooltipTextLayout, ViewportNarrowerThanOneGlyphStillShowsTheEllipsis)
{
	const auto Layout = Measure("中文末字", vec2(14, 100));
	ASSERT_TRUE(Layout.m_Truncated);
	ASSERT_GT(Layout.m_VisibleLines, 0);
	CContainer End(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, "…", vec2(0, 0), End.m_Index, true);
	ASSERT_TRUE(End.m_Index.Valid());
	EXPECT_EQ(Cursor.m_GlyphCount, 1);
	EXPECT_LE(Cursor.m_LongestLineWidth, Layout.m_LineWidth + 0.001f);
}

TEST_F(QmTooltipTextLayout, ExactSingleLineHeightDoesNotBecomeTruncatedByFloatRounding)
{
	const char *pText = "最后一字Z";
	const auto Full = Measure(pText, vec2(500, 300), 300, 13.37f);
	const auto Exact = Measure(pText, vec2(500, Full.m_Size.y), 300, 13.37f);
	ASSERT_EQ(Exact.m_VisibleLines, 1);
	ASSERT_FALSE(Exact.m_Truncated);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Exact, pText, vec2(5, 5 + Exact.m_TextOffsetY), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	EXPECT_EQ(Cursor.m_LineCount, 1);
}

TEST_F(QmTooltipTextLayout, GlyphVerticalExtentsFitInsideThePaddedBubble)
{
	const char *pText = "Ágj中文\n最後一字Z";
	const auto Layout = Measure(pText, vec2(500, 300), 300, 23.7f);
	ASSERT_FALSE(Layout.m_Truncated);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, vec2(Layout.m_Padding + Layout.m_TextOffsetX, Layout.m_Padding + Layout.m_TextOffsetY), Container.m_Index);
	ASSERT_TRUE(Cursor.m_HasVisualBoundingBox);
	EXPECT_GE(Cursor.m_VisualTop, Layout.m_Padding - 0.001f);
	EXPECT_LE(Cursor.m_VisualBottom, Layout.m_Size.y - Layout.m_Padding + 0.001f);
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
}

TEST_F(QmTooltipTextLayout, AnimationAndSettlingKeepTheSameGlyphsLinesAndContainerBounds)
{
	const char *pText = "动画中保持中文末字\nEnglish words keep their final Z";
	const auto Layout = Measure(pText, vec2(500, 300), 110, 13.37f);
	ASSERT_FALSE(Layout.m_Truncated);
	const CUIRect Screen{0, 0, 600, 400};
	const CUIRect Fixed = QmTooltipRect({200, 180, 100, 20}, Screen, Layout.m_Size, 5);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, Fixed.TopLeft() + vec2(Layout.m_Padding + Layout.m_TextOffsetX, Layout.m_Padding + Layout.m_TextOffsetY), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	ASSERT_EQ(Cursor.m_CharCount, str_length(pText));
	const auto Before = TextRender().GetBoundingBoxTextContainer(Container.m_Index);
	auto *pGraphics = static_cast<CIconBenchmarkGraphics *>(m_Ui.Graphics());
	for(float Elapsed : std::array<float, 7>{0.0f, 0.025f, 0.08f, 0.12f, 0.17f, 0.18f, 1.0f})
	{
		SCOPED_TRACE(Elapsed);
		const CUIRect Bubble = QmTooltipAnimatedRect(Fixed, Screen, QmTooltipScale(Elapsed, true));
		const CUIRect Projection = QmTooltipTextProjection(Screen, Fixed.Center(), Bubble.w / Fixed.w);
		m_Ui.Graphics()->MapScreen(Projection.x, Projection.y, Projection.x + Projection.w, Projection.y + Projection.h);
		const uint64_t QuadsBefore = pGraphics->m_Quads;
		TextRender().RenderTextContainer(Container.m_Index, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1));
		EXPECT_EQ(pGraphics->m_Quads - QuadsBefore, static_cast<uint64_t>(Cursor.m_GlyphCount));
		const auto After = TextRender().GetBoundingBoxTextContainer(Container.m_Index);
		EXPECT_FLOAT_EQ(After.m_W, Before.m_W);
		EXPECT_FLOAT_EQ(After.m_H, Before.m_H);
		EXPECT_FLOAT_EQ(After.m_H, Cursor.Height());
	}
	m_Ui.Graphics()->MapScreen(Screen.x, Screen.y, Screen.x + Screen.w, Screen.y + Screen.h);
	CContainer Restored(TextRender());
	const auto RestoredCursor = QmTooltipCreateText(TextRender(), Layout, pText, Fixed.TopLeft() + vec2(Layout.m_Padding + Layout.m_TextOffsetX, Layout.m_Padding + Layout.m_TextOffsetY), Restored.m_Index);
	EXPECT_EQ(RestoredCursor.m_CharCount, Cursor.m_CharCount);
	EXPECT_EQ(RestoredCursor.m_GlyphCount, Cursor.m_GlyphCount);
	EXPECT_EQ(RestoredCursor.m_LineCount, Cursor.m_LineCount);
}

TEST_F(QmTooltipTextLayout, EmptyViewportDoesNotCreateTextAndRestoresCallerRenderFlags)
{
	const unsigned Previous = TextRender().GetRenderFlags();
	TextRender().SetRenderFlags(TEXT_RENDER_FLAG_KERNING);
	const auto Empty = Measure("hidden", vec2(0, 0));
	CContainer Container(TextRender());
	QmTooltipCreateText(TextRender(), Empty, "hidden", vec2(0, 0), Container.m_Index);
	EXPECT_FALSE(Container.m_Index.Valid());
	EXPECT_EQ(Empty.m_VisibleLines, 0);
	EXPECT_EQ(TextRender().GetRenderFlags(), static_cast<unsigned>(TEXT_RENDER_FLAG_KERNING));
	const auto Full = Measure("visible");
	QmTooltipCreateText(TextRender(), Full, "visible", vec2(0, 0), Container.m_Index);
	EXPECT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(TextRender().GetRenderFlags(), static_cast<unsigned>(TEXT_RENDER_FLAG_KERNING));
	TextRender().SetRenderFlags(Previous);
}

TEST_F(QmTooltipTextLayout, TransparentCallerTextColorDoesNotMakeTooltipGlyphsDisappear)
{
	const ColorRGBA Previous = TextRender().GetTextColor();
	const ColorRGBA Caller(0.2f, 0.4f, 0.6f, 0.0f);
	TextRender().TextColor(Caller);
	const auto Layout = Measure("VisibleLastZ");
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, "VisibleLastZ", vec2(5, 5), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(TextRender().GetTextColor(), Caller);
	auto *pGraphics = static_cast<CIconBenchmarkGraphics *>(m_Ui.Graphics());
	const uint64_t Before = pGraphics->m_Quads;
	TextRender().RenderTextContainer(Container.m_Index, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1));
	EXPECT_EQ(pGraphics->m_Quads - Before, static_cast<uint64_t>(Cursor.m_GlyphCount));
	EXPECT_GT(Cursor.m_GlyphCount, 0);
	TextRender().TextColor(Previous);
}

TEST_F(QmTooltipTextLayout, IconPresetAndSpecialFlagsUseOrdinaryTextAndRestoreCallerState)
{
	const char *pText = "完整中文末字Z";
	const auto Baseline = Measure(pText, vec2(500, 300), 120, 13.37f);
	CContainer Reference(TextRender());
	const auto ReferenceCursor = QmTooltipCreateText(TextRender(), Baseline, pText, vec2(5, 5), Reference.m_Index);
	ASSERT_TRUE(Reference.m_Index.Valid());
	const unsigned PreviousFlags = TextRender().GetRenderFlags();
	const EFontPreset PreviousPreset = TextRender().GetFontPreset();
	const ColorRGBA PreviousColor = TextRender().GetTextColor();
	const unsigned CallerFlags = TEXT_RENDER_FLAG_QM_NAMEPLATE | TEXT_RENDER_FLAG_NO_X_BEARING |
				     TEXT_RENDER_FLAG_NO_Y_BEARING | TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH |
				     TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING | TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE |
				     TEXT_RENDER_FLAG_NO_AUTOMATIC_QUAD_UPLOAD;
	for(EFontPreset Preset : std::array<EFontPreset, 2>{EFontPreset::ICON_FONT, EFontPreset::ICON_FONT_BOLD})
	{
		SCOPED_TRACE(static_cast<int>(Preset));
		TextRender().SetFontPreset(Preset);
		TextRender().SetRenderFlags(CallerFlags);
		TextRender().TextColor(ColorRGBA(0.2f, 0.4f, 0.6f, 0.0f));
		const ColorRGBA CallerColor = TextRender().GetTextColor();
		const auto Layout = Measure(pText, vec2(500, 300), 120, 13.37f);
		EXPECT_EQ(TextRender().GetFontPreset(), Preset);
		EXPECT_EQ(TextRender().GetRenderFlags(), CallerFlags);
		EXPECT_EQ(TextRender().GetTextColor(), CallerColor);
		EXPECT_EQ(Layout.m_LineCount, Baseline.m_LineCount);
		EXPECT_FLOAT_EQ(Layout.m_FontSize, Baseline.m_FontSize);
		EXPECT_FLOAT_EQ(Layout.m_Size.x, Baseline.m_Size.x);
		EXPECT_FLOAT_EQ(Layout.m_Size.y, Baseline.m_Size.y);
		EXPECT_FALSE(Layout.m_Truncated);
		CContainer Container(TextRender());
		const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, vec2(5, 5), Container.m_Index);
		EXPECT_TRUE(Container.m_Index.Valid());
		EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
		EXPECT_EQ(Cursor.m_GlyphCount, ReferenceCursor.m_GlyphCount);
		EXPECT_EQ(Cursor.m_LineCount, ReferenceCursor.m_LineCount);
		EXPECT_FLOAT_EQ(Cursor.m_LongestLineWidth, ReferenceCursor.m_LongestLineWidth);
		EXPECT_EQ(TextRender().GetFontPreset(), Preset);
		EXPECT_EQ(TextRender().GetRenderFlags(), CallerFlags);
		EXPECT_EQ(TextRender().GetTextColor(), CallerColor);
		auto *pGraphics = static_cast<CIconBenchmarkGraphics *>(m_Ui.Graphics());
		const uint64_t Before = pGraphics->m_Quads;
		TextRender().RenderTextContainer(Container.m_Index, ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1));
		EXPECT_EQ(pGraphics->m_Quads - Before, static_cast<uint64_t>(ReferenceCursor.m_GlyphCount));
	}
	TextRender().SetRenderFlags(PreviousFlags);
	TextRender().SetFontPreset(PreviousPreset);
	TextRender().TextColor(PreviousColor);
}

TEST_F(QmTooltipTextLayout, NonIntegralPixelScaleUsesTheSameFontOnceForMeasureAndCreate)
{
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	pGraphics->SetFramebufferSize(1280, 720);
	pGraphics->MapScreen(0, 0, 1066.6667f, 600);
	const char *pText = "最后一字Z LastZ";
	const auto Layout = Measure(pText, vec2(300, 200), 72, 13);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText, vec2(5, 5), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_FLOAT_EQ(Layout.m_FontSize, 13);
	EXPECT_FLOAT_EQ(Cursor.m_AlignedFontSize, Layout.m_LineHeight);
	EXPECT_EQ(Cursor.m_LineCount, Layout.m_LineCount);
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
}

TEST_F(QmTooltipTextLayout, ChineseAndLatinInkHaveEqualHorizontalPadding)
{
	for(const char *pText : {"提示文字", "导出截图", "LastZ", " Ágj ", "中文末字Z"})
	{
		SCOPED_TRACE(pText);
		const auto Layout = Measure(pText);
		ASSERT_FALSE(Layout.m_Truncated);
		CContainer Container(TextRender());
		const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText,
			vec2(Layout.m_Padding + Layout.m_TextOffsetX, Layout.m_Padding + Layout.m_TextOffsetY), Container.m_Index);
		ASSERT_TRUE(Container.m_Index.Valid());
		ASSERT_TRUE(Cursor.m_HasVisualBoundingBox);
		EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
		EXPECT_NEAR(Cursor.m_VisualLeft, Layout.m_Padding, 0.001f);
		EXPECT_NEAR(Layout.m_Size.x - Cursor.m_VisualRight, Layout.m_Padding, 0.001f);
	}
}

TEST_F(QmTooltipTextLayout, WrappedInkKeepsEqualPaddingAtFractionalPixelScale)
{
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	pGraphics->SetFramebufferSize(1280, 720);
	pGraphics->MapScreen(0, 0, 1066.6667f, 600);
	const char *pText = "中文末字Z\nÁgj words wrap inside the tooltip";
	const auto Layout = Measure(pText, vec2(300, 200), 78, 13.37f);
	ASSERT_FALSE(Layout.m_Truncated);
	ASSERT_GT(Layout.m_LineCount, 1);
	CContainer Container(TextRender());
	const auto Cursor = QmTooltipCreateText(TextRender(), Layout, pText,
		vec2(Layout.m_Padding + Layout.m_TextOffsetX, Layout.m_Padding + Layout.m_TextOffsetY), Container.m_Index);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	EXPECT_EQ(Cursor.m_LineCount, Layout.m_LineCount);
	EXPECT_NEAR(Cursor.m_VisualLeft, Layout.m_Padding, 0.001f);
	EXPECT_NEAR(Layout.m_Size.x - Cursor.m_VisualRight, Layout.m_Padding, 0.001f);
}
