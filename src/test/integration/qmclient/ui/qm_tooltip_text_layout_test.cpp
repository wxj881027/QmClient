// 真实文字引擎与共享 Tooltip 布局协作；仅 GPU 设备提交由 fixture 替换。
#include <base/str.h>

#include <game/client/components/tooltips.h>

#include <test/support/qm_real_ui_fixture.h>

#include <array>
#include <string>

namespace
{
	// 设置页 → DDNet → 游戏玩法卡「预测余量」行的配置说明（含旧名）。
	// 用户实测该 tooltip 只剩一个 …，完整说明完全看不到。
	const char *const PREDICTION_MARGIN_HELP = "预测边界（单位：毫秒，增加延迟但可减少因 ping 波动导致的卡顿；旧名「预测边距」）";

	class QmTooltipTextLayout : public qm_ui_test::CRealUiFixture
	{
	protected:
		ITextRender &TextRender() { return *m_Ui.TextRender(); }
		SQmTooltipTextLayout Measure(const char *pText, vec2 Available = vec2(500, 300), float Width = 300, float Font = 14)
		{
			return QmTooltipMeasureText(TextRender(), pText, Font, Width, Available, 5);
		}

		// 气泡排版的生产入口：字号/宽度/屏幕按调用点传入，测试不复制实现里的宽高公式。
		SQmTooltipTextFacts ResolveFacts(const char *pText, vec2 Screen = vec2(1066.0f, 600.0f), float Font = 10.0f, float WidthHint = -1.0f)
		{
			return QmResolveTooltipText(TextRender(), pText, Font, WidthHint, {0.0f, 0.0f, Screen.x, Screen.y});
		}

		struct CCachedContainer
		{
			ITextRender &m_TextRender;
			CQmTooltipTextCache m_Cache;
			explicit CCachedContainer(ITextRender &TextRender) : m_TextRender(TextRender) {}
			~CCachedContainer() { m_Cache.Clear(m_TextRender); }
		};

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

TEST_F(QmTooltipTextLayout, CachedTextKeepsFinalGlyphsInsideClipWhileBubbleMovesAndResizes)
{
	const CUIRect Screen{100, 50, 600, 400};
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	pGraphics->SetFramebufferSize(1280, 720);
	pGraphics->MapScreen(Screen.x, Screen.y, Screen.x + Screen.w, Screen.y + Screen.h);
	const char *pText = "切换气泡保留最后字\nEnglish final Z";
	const auto Layout = Measure(pText, vec2(500, 300), 110, 13.37f);
	ASSERT_FALSE(Layout.m_Truncated);
	const CUIRect Target = QmTooltipRect({400, 230, 100, 20}, Screen, Layout.m_Size, 5);
	CContainer Reference(TextRender());
	const auto Expected = QmTooltipCreateText(TextRender(), Layout, pText, vec2(0, 0), Reference.m_Index);
	ASSERT_EQ(Expected.m_CharCount, str_length(pText));
	const vec2 Offset = Target.TopLeft() + vec2(Layout.m_Padding + Layout.m_TextOffsetX, Layout.m_Padding + Layout.m_TextOffsetY);
	// 宽气泡变高与窄气泡变宽分别让高度、宽度成为统一比例的限制项。
	for(const CUIRect Initial : std::array<CUIRect, 2>{{{120, 70, 220, 16}, {180, 120, 24, 160}}})
	{
		SCOPED_TRACE(Initial.w);
		CCachedContainer Container(TextRender());
		auto &Cache = Container.m_Cache;
		CQmTooltipMotionState Motion;
		Motion.Update(Initial, 0, true);
		Motion.Update(Target, 1, true);
		int FirstIndex = -1;
		uint64_t FirstUploads = 0;
		for(double Now : std::array<double, 5>{1.0, 1.04, 1.08, 1.16, 1.3})
		{
			SCOPED_TRACE(Now);
			const CUIRect Bubble = QmTooltipAnimatedRect(Motion.Update(Target, Now, true), Screen, QmTooltipScale(0.25f, true));
			const CUIRect Projection = QmTooltipTextProjection(Screen, Target, Bubble);
			pGraphics->MapScreen(Screen.x, Screen.y, Screen.x + Screen.w, Screen.y + Screen.h);
			{
				CQmTooltipTextScope Scope(TextRender());
				Cache.Update(TextRender(), QmTooltipTextCursor({}, Layout.m_FontSize, Layout.m_LineWidth, 0), pText);
			}
			ASSERT_TRUE(Cache.Index().Valid());
			const CTextCursor &Cached = Cache.LayoutCursor();
			ASSERT_TRUE(Cached.m_HasVisualBoundingBox);
			EXPECT_EQ(Cached.m_CharCount, str_length(pText));
			EXPECT_EQ(Cached.m_LineCount, Expected.m_LineCount);
			EXPECT_FLOAT_EQ(Cached.Height(), Expected.Height());
			const bool FirstFrame = FirstIndex < 0;
			if(FirstFrame)
				FirstIndex = Cache.Index().m_Index;
			EXPECT_EQ(Cache.Index().m_Index, FirstIndex);
			m_Ui.ClipEnable(&Bubble);
			pGraphics->MapScreen(Projection.x, Projection.y, Projection.x + Projection.w, Projection.y + Projection.h);
			const uint64_t Before = pGraphics->m_Quads;
			TextRender().RenderTextContainer(Cache.Index(), ColorRGBA(1, 1, 1, 1), ColorRGBA(0, 0, 0, 1), Offset.x, Offset.y);
			EXPECT_EQ(pGraphics->m_Quads - Before, static_cast<uint64_t>(Expected.m_GlyphCount));
			// 首次绘制允许刷新待上传字形；后续投影变化不能重新上传正文。
			if(FirstFrame)
				FirstUploads = pGraphics->m_Uploads;
			EXPECT_EQ(pGraphics->m_Uploads, FirstUploads);
			float X0, Y0, X1, Y1;
			pGraphics->GetScreen(&X0, &Y0, &X1, &Y1);
			// 对真实缓存的填充范围应用设备投影，包含全部末字，而非只数提交的 quad。
			const auto Project = [&](vec2 Point) {
				return vec2(Screen.x + (Point.x - X0) * Screen.w / (X1 - X0), Screen.y + (Point.y - Y0) * Screen.h / (Y1 - Y0));
			};
			const vec2 TopLeft = Project(Offset + vec2(Cached.m_VisualLeft, Cached.m_VisualTop));
			const vec2 BottomRight = Project(Offset + vec2(Cached.m_VisualRight, Cached.m_VisualBottom));
			EXPECT_GE(TopLeft.x, Bubble.x - 0.001f);
			EXPECT_GE(TopLeft.y, Bubble.y - 0.001f);
			EXPECT_LE(BottomRight.x, Bubble.x + Bubble.w + 0.001f);
			EXPECT_LE(BottomRight.y, Bubble.y + Bubble.h + 0.001f);
			pGraphics->MapScreen(Screen.x, Screen.y, Screen.x + Screen.w, Screen.y + Screen.h);
			m_Ui.ClipDisable();
		}
	}
}

TEST_F(QmTooltipTextLayout, SettingsConfigHelpWrapsIntoMultipleReadableLines)
{
	const auto Facts = ResolveFacts(PREDICTION_MARGIN_HELP);
	ASSERT_FALSE(Facts.m_Layout.m_Truncated);
	ASSERT_GE(Facts.m_Layout.m_LineCount, 2);
	// 未截断时正文不限行，且不画省略号标记。
	EXPECT_EQ(Facts.m_BodyMaxLines, 0);
	EXPECT_FALSE(Facts.m_DrawEllipsis);
	CContainer Container(TextRender());
	CTextCursor Cursor = QmTooltipTextCursor({}, Facts.m_FontSize, Facts.m_WrapWidth, Facts.m_BodyMaxLines);
	TextRender().CreateTextContainer(Container.m_Index, &Cursor, PREDICTION_MARGIN_HELP);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(PREDICTION_MARGIN_HELP));
	EXPECT_FALSE(Cursor.m_Truncated);
	// 气泡按实际字宽走，不超过可读行宽；高度覆盖全部换行行。
	EXPECT_LE(Facts.m_Layout.m_Size.x, Facts.m_WrapWidth + 2.0f * Facts.m_Layout.m_Padding + 0.001f);
}

TEST_F(QmTooltipTextLayout, ShortSettingsHintKeepsASingleNarrowBubble)
{
	const auto Facts = ResolveFacts("预测余量");
	ASSERT_EQ(Facts.m_Layout.m_LineCount, 1);
	ASSERT_FALSE(Facts.m_Layout.m_Truncated);
	EXPECT_EQ(Facts.m_BodyMaxLines, 0);
	EXPECT_FALSE(Facts.m_DrawEllipsis);
	// 短文本不能被拉成整条可读行宽的气泡。
	EXPECT_LT(Facts.m_Layout.m_Size.x, Facts.m_WrapWidth);
}

TEST_F(QmTooltipTextLayout, CompleteSingleLineHintIsNotMarkedTruncated)
{
	// 用户实机那条说明的单行子集：整段只占一行、每个字形都完整可见。
	// 行宽溢出只记在 m_LineOverflow（排版/避让用），不能冒充「截断」——
	// 否则绘制侧会据此走省略号兜底，把唯一一行正文整段丢掉。
	const char *pText = "预测边界（毫秒）";
	const auto Facts = ResolveFacts(pText);
	ASSERT_EQ(Facts.m_Layout.m_LineCount, 1);
	ASSERT_EQ(Facts.m_Layout.m_VisibleLines, Facts.m_Layout.m_LineCount);
	ASSERT_FALSE(Facts.m_Layout.m_Truncated);
	EXPECT_FALSE(Facts.m_DrawEllipsis);
	// 完整可见：真实文字引擎把整段文字都排进这一行，并且不画省略号标记。
	CContainer Container(TextRender());
	CTextCursor Cursor = QmTooltipTextCursor({}, Facts.m_FontSize, Facts.m_WrapWidth, Facts.m_BodyMaxLines);
	TextRender().CreateTextContainer(Container.m_Index, &Cursor, pText);
	ASSERT_TRUE(Container.m_Index.Valid());
	EXPECT_EQ(Cursor.m_CharCount, str_length(pText));
	EXPECT_FALSE(Cursor.m_Truncated);
	EXPECT_FALSE(QmTooltipDrawEllipsis(Facts, Cursor.m_GlyphCount));
}

TEST_F(QmTooltipTextLayout, SingleVisibleLineNeverLeavesTheEllipsisAsTheOnlyContent)
{
	// 只管高度、不管宽度的可用区：从「放不下一行」扫到「能放几行」，
	// 覆盖「只够一行 + 文本需要多行」这条把正文整段丢掉的边界。
	int OneVisibleLineCases = 0;
	for(float Height = 16.0f; Height <= 64.0f; Height += 2.0f)
	{
		SCOPED_TRACE(Height);
		const auto Facts = ResolveFacts(PREDICTION_MARGIN_HELP, vec2(300.0f, Height));
		EXPECT_LE(Facts.m_Layout.m_Size.x, 300.0f);
		EXPECT_LE(Facts.m_Layout.m_Size.y, Height);
		if(Facts.m_Layout.m_VisibleLines <= 0)
			continue;
		CContainer Body(TextRender());
		CTextCursor BodyCursor = QmTooltipTextCursor({}, Facts.m_FontSize, Facts.m_WrapWidth, Facts.m_BodyMaxLines);
		TextRender().CreateTextContainer(Body.m_Index, &BodyCursor, PREDICTION_MARGIN_HELP);
		ASSERT_GT(BodyCursor.m_GlyphCount, 0);
		if(!Facts.m_Layout.m_Truncated)
		{
			EXPECT_EQ(Facts.m_BodyMaxLines, 0);
			EXPECT_FALSE(Facts.m_DrawEllipsis);
			continue;
		}
		// 截断时必须先保证正文有行可画；省略号只能在「正文之后还有更多行」时出现。
		EXPECT_GE(Facts.m_BodyMaxLines, 1);
		if(Facts.m_Layout.m_VisibleLines == 1)
		{
			++OneVisibleLineCases;
			EXPECT_FALSE(Facts.m_DrawEllipsis) << "只够一行时仍画省略号，正文会被整段丢掉";
			EXPECT_FALSE(QmTooltipDrawEllipsis(Facts, BodyCursor.m_GlyphCount));
		}
		else
		{
			EXPECT_TRUE(Facts.m_DrawEllipsis);
			EXPECT_TRUE(QmTooltipDrawEllipsis(Facts, BodyCursor.m_GlyphCount));
		}
	}
	// 这一组高度里必须真的出现「只够一行」的测量结果，否则上面的断言没覆盖到 bug 现场。
	EXPECT_GT(OneVisibleLineCases, 0);
}

TEST_F(QmTooltipTextLayout, NarrowWidthNeverDrawsTheEllipsisWithoutABodyLine)
{
	// 宽度从「一个字形都放不下」扫到默认可读宽度：气泡始终留在给定宽度内，
	// 且只要画省略号就必须同时留出一整行正文——省略号不能是气泡的唯一内容。
	for(float Width = 16.0f; Width <= 320.0f; Width += 8.0f)
	{
		SCOPED_TRACE(Width);
		const auto Facts = ResolveFacts(PREDICTION_MARGIN_HELP, vec2(Width, 400.0f));
		EXPECT_LE(Facts.m_Layout.m_Size.x, Width);
		if(Facts.m_Layout.m_VisibleLines <= 0)
			continue;
		CContainer Body(TextRender());
		CTextCursor BodyCursor = QmTooltipTextCursor({}, Facts.m_FontSize, Facts.m_WrapWidth, Facts.m_BodyMaxLines);
		TextRender().CreateTextContainer(Body.m_Index, &BodyCursor, PREDICTION_MARGIN_HELP);
		if(!QmTooltipDrawEllipsis(Facts, BodyCursor.m_GlyphCount))
			continue;
		EXPECT_GT(Facts.m_Layout.m_VisibleLines, 1);
	}
}

TEST_F(QmTooltipTextLayout, OverlongHintKeepsTheEllipsisOnTheLastVisibleLineInsideTheScreen)
{
	const vec2 Screen(70.0f, 60.0f);
	const auto Facts = ResolveFacts(PREDICTION_MARGIN_HELP, Screen);
	ASSERT_TRUE(Facts.m_Layout.m_Truncated);
	ASSERT_GT(Facts.m_Layout.m_VisibleLines, 1);
	EXPECT_LE(Facts.m_Layout.m_Size.x, Screen.x);
	EXPECT_LE(Facts.m_Layout.m_Size.y, Screen.y);
	EXPECT_TRUE(Facts.m_DrawEllipsis);
	EXPECT_EQ(Facts.m_BodyMaxLines, Facts.m_Layout.m_VisibleLines - 1);
	CContainer Body(TextRender()), End(TextRender());
	CTextCursor BodyCursor = QmTooltipTextCursor({}, Facts.m_FontSize, Facts.m_WrapWidth, Facts.m_BodyMaxLines);
	TextRender().CreateTextContainer(Body.m_Index, &BodyCursor, PREDICTION_MARGIN_HELP);
	ASSERT_TRUE(Body.m_Index.Valid());
	EXPECT_LE(BodyCursor.m_LineCount, Facts.m_Layout.m_VisibleLines - 1);
	EXPECT_LT(BodyCursor.m_CharCount, str_length(PREDICTION_MARGIN_HELP));
	EXPECT_TRUE(QmTooltipDrawEllipsis(Facts, BodyCursor.m_GlyphCount));
	const float EllipsisY = Facts.m_Layout.m_Padding + Facts.m_Layout.m_TextOffsetY + (Facts.m_Layout.m_VisibleLines - 1) * Facts.m_Layout.m_LineHeight;
	const auto EndCursor = QmTooltipCreateText(TextRender(), Facts.m_Layout, "…", vec2(Facts.m_Layout.m_Padding + Facts.m_Layout.m_TextOffsetX, EllipsisY), End.m_Index, true);
	ASSERT_TRUE(End.m_Index.Valid());
	EXPECT_EQ(EndCursor.m_GlyphCount, 1);
	// 省略号落在气泡内、且在屏幕内：不溢出。
	ASSERT_TRUE(EndCursor.m_HasVisualBoundingBox);
	// 文字引擎的可视包围盒是相对容器原点的绝对坐标（测量入口也用 m_VisualTop 直接反推
	// 偏移），不能再加一次 EllipsisY：重复计入会把 42 变成 76，误判成溢出。
	EXPECT_LE(EndCursor.m_VisualBottom, Facts.m_Layout.m_Size.y + 0.001f);
}

TEST(QmTooltipFontScale, ConfiguredValueIsTheRenderedFontSize)
{
	// 配置值就是字号本身：默认 10 不缩放；老存档里调到 20 的用户升级后
	// 仍得到 20，而不是按「14 = 1 倍」二次换算掉到 14.3。
	EXPECT_FLOAT_EQ(QmTooltipFontScale(10), 1.0f);
	EXPECT_FLOAT_EQ(QmTooltipFontScale(20), 2.0f);
	EXPECT_FLOAT_EQ(ui_token::tooltip::FONT_SIZE * QmTooltipFontScale(20), 20.0f);
	// 设置页预览按配置值直接画字：合法范围内预览字号必须严格等于真实气泡字号。
	for(int FontSize = 10; FontSize <= 24; ++FontSize)
		EXPECT_FLOAT_EQ(ui_token::tooltip::FONT_SIZE * QmTooltipFontScale(FontSize), static_cast<float>(FontSize));
	// 越界存档按配置范围夹住，不让极端值把气泡撑出屏幕。
	EXPECT_FLOAT_EQ(ui_token::tooltip::FONT_SIZE * QmTooltipFontScale(1), 10.0f);
	EXPECT_FLOAT_EQ(ui_token::tooltip::FONT_SIZE * QmTooltipFontScale(99), 24.0f);
}
