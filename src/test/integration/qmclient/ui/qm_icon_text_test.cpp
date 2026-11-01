// 真实 UI 标签与字体字面、缓存失效协作；仅隔离 GPU 设备。
#include <engine/textrender.h>

#include <game/client/qm_icon.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmIconText : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUIElement::SUIElementRect m_Label;
		CUIRect m_Rect{0, 0, 80, 24};

		void TearDown() override
		{
			m_Ui.TextRender()->DeleteTextContainer(m_Label.m_UITextContainer);
			CRealUiFixture::TearDown();
		}
		bool Draw(const char *pGlyph)
		{
			bool Recreated = false;
			m_Ui.DoLabelStreamed(m_Label, &m_Rect, pGlyph, 14, TEXTALIGN_MC, {}, -1, nullptr, true, &Recreated);
			return Recreated;
		}
		void PrepareCallerState()
		{
			g_Config.m_QmUiFriendIconFilled = 1;
			auto *pText = m_Ui.TextRender();
			pText->SetFontPreset(EFontPreset::DEFAULT_FONT);
			pText->SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
			pText->TextColor({0.2f, 0.3f, 0.4f, 0.5f});
			pText->TextOutlineColor({0.7f, 0.6f, 0.5f, 0.4f});
			pText->TextSelectionColor({0.8f, 0.7f, 0.6f, 0.3f});
		}
		void ExpectCallerState()
		{
			const auto *pText = m_Ui.TextRender();
			EXPECT_EQ(pText->GetFontPreset(), EFontPreset::DEFAULT_FONT);
			EXPECT_EQ(pText->GetRenderFlags(), TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
			EXPECT_EQ(pText->GetTextColor(), ColorRGBA(0.2f, 0.3f, 0.4f, 0.5f));
			EXPECT_EQ(pText->GetTextOutlineColor(), ColorRGBA(0.7f, 0.6f, 0.5f, 0.4f));
			EXPECT_EQ(pText->GetTextSelectionColor(), ColorRGBA(0.8f, 0.7f, 0.6f, 0.3f));
		}
		bool DrawDirect()
		{
			return m_Ui.DrawQmIcon(m_Rect, EQmIcon::HEART, FontIcons::FONT_ICON_HEART, {0.9f, 0.3f, 0.1f, 0.4f});
		}
	};
}

TEST_F(QmIconText, FilledHeartRebuildsStreamedContainerAndRestoresCallerPreset)
{
	m_Ui.TextRender()->SetFontPreset(EFontPreset::ICON_FONT_BOLD);
	g_Config.m_QmUiFriendIconFilled = 0;
	ASSERT_TRUE(Draw(FontIcons::FONT_ICON_HEART));
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::ICON_FONT_BOLD);
	EXPECT_FALSE(Draw(FontIcons::FONT_ICON_HEART));
	g_Config.m_QmUiFriendIconFilled = 1;
	EXPECT_TRUE(Draw(FontIcons::FONT_ICON_HEART));
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::ICON_FONT_FILL);
	EXPECT_EQ(m_Ui.TextRender()->GetFontPreset(), EFontPreset::ICON_FONT_BOLD);
	EXPECT_FALSE(Draw(FontIcons::FONT_ICON_HEART));
	g_Config.m_QmUiFriendIconFilled = 0;
	EXPECT_TRUE(Draw(FontIcons::FONT_ICON_HEART));
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::ICON_FONT_BOLD);
}

TEST_F(QmIconText, FavoriteFillDoesNotChangeFriendOrOrdinaryText)
{
	m_Ui.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	g_Config.m_QmUiFavoriteIconFilled = 1;
	g_Config.m_QmUiFriendIconFilled = 0;
	ASSERT_TRUE(Draw(FontIcons::FONT_ICON_STAR));
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::ICON_FONT_FILL);
	ASSERT_TRUE(Draw(FontIcons::FONT_ICON_HEART));
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::ICON_FONT);
	m_Ui.TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	g_Config.m_QmUiFriendIconFilled = 1;
	ASSERT_TRUE(Draw("Body text"));
	EXPECT_EQ(m_Label.m_NumQmIcons, 0);
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::DEFAULT_FONT);
}

TEST_F(QmIconText, DirectContainerUsesFillAndImmediateDrawingRestoresTextState)
{
	m_Ui.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	g_Config.m_QmUiFriendIconFilled = 1;
	m_Ui.DoLabel(m_Label, &m_Rect, FontIcons::FONT_ICON_HEART, 14, TEXTALIGN_MC);
	EXPECT_TRUE(m_Label.m_UITextContainer.Valid());
	EXPECT_EQ(m_Label.m_FontPreset, EFontPreset::ICON_FONT_FILL);
	const auto Color = m_Ui.TextRender()->GetTextColor();
	const unsigned Flags = m_Ui.TextRender()->GetRenderFlags();
	m_Ui.DoLabel(&m_Rect, FontIcons::FONT_ICON_HEART, 14, TEXTALIGN_MC);
	EXPECT_EQ(m_Ui.TextRender()->GetFontPreset(), EFontPreset::ICON_FONT);
	EXPECT_EQ(m_Ui.TextRender()->GetTextColor(), Color);
	EXPECT_EQ(m_Ui.TextRender()->GetRenderFlags(), Flags);
}

// 灵动岛连接质量图标（用户三次反馈「图标没有」）：cell-signal 四档必须能在
// 真实嵌入字体下绘制。与 hud.cpp 渲染块同参数（~13px 小矩形、ICON_FONT 预设）。
TEST_F(QmIconText, CellSignalGlyphsDrawThroughQmIconApi)
{
	const CUIRect IconRect{10.0f, 10.0f, 13.0f, 13.0f};
	const float FontSize = QmIconFontSize(IconRect);
	const struct
	{
		EQmIcon m_Icon;
		const char *m_pGlyph;
	} aCases[] = {
		{EQmIcon::CELL_SIGNAL_HIGH, FontIcons::FONT_ICON_CELL_SIGNAL_HIGH},
		{EQmIcon::CELL_SIGNAL_MEDIUM, FontIcons::FONT_ICON_CELL_SIGNAL_MEDIUM},
		{EQmIcon::CELL_SIGNAL_LOW, FontIcons::FONT_ICON_CELL_SIGNAL_LOW},
		{EQmIcon::CELL_SIGNAL_NONE, FontIcons::FONT_ICON_CELL_SIGNAL_NONE},
	};
	for(const auto &Case : aCases)
	{
		// 码位必须在 ICON_FONT（Regular）字面中有真实字形：宽度>0 且非回退方框宽度。
		m_Ui.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		const float GlyphWidth = m_Ui.TextRender()->TextWidth(FontSize, Case.m_pGlyph);
		EXPECT_GT(GlyphWidth, 0.5f) << "cell-signal 字形缺失或零宽（码位未覆盖）";
		// DrawQmIcon 必须返回 true（非空字面量）且恢复调用方 preset/颜色/标志。
		const auto PrevColor = m_Ui.TextRender()->GetTextColor();
		const unsigned PrevFlags = m_Ui.TextRender()->GetRenderFlags();
		const EFontPreset PrevPreset = m_Ui.TextRender()->GetFontPreset();
		EXPECT_TRUE(m_Ui.DrawQmIcon(IconRect, Case.m_Icon, Case.m_pGlyph, ColorRGBA(0.6f, 0.64f, 0.7f, 1.0f)));
		EXPECT_EQ(m_Ui.TextRender()->GetFontPreset(), PrevPreset);
		EXPECT_EQ(m_Ui.TextRender()->GetTextColor(), PrevColor);
		EXPECT_EQ(m_Ui.TextRender()->GetRenderFlags(), PrevFlags);
	}
}

TEST_F(QmIconText, FilledIconDrawRestoresCallerFontFlagsAndAllColors)
{
	PrepareCallerState();
	const auto Draws = Graphics().m_Draws;
	const auto Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	EXPECT_TRUE(DrawDirect());
	EXPECT_GT(Graphics().m_Draws, Draws);
	ExpectCallerState();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmIconText, PartialIconUploadRestoresOuterStateBeforeDeferredCleanupAndRetry)
{
	PrepareCallerState();
	const auto Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailBufferContainerCreate = true;
	EXPECT_THROW(DrawDirect(), std::bad_alloc);
	EXPECT_FALSE(Graphics().m_FailBufferContainerCreate);
	ExpectCallerState();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_NO_THROW(m_Ui.TextRender()->QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_TRUE(DrawDirect());
	ExpectCallerState();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}

TEST_F(QmIconText, IconDrawFailureRestoresOuterStateAndAllowsFollowingFrameToDraw)
{
	PrepareCallerState();
	const auto Buffers = Graphics().m_Buffers.size(), Containers = Graphics().m_Containers.size();
	Graphics().m_FailTextDraw = true;
	EXPECT_THROW(DrawDirect(), std::bad_alloc);
	EXPECT_FALSE(Graphics().m_FailTextDraw);
	ExpectCallerState();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers + 1);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers + 1);
	EXPECT_NO_THROW(m_Ui.TextRender()->QmTextFrameEnd());
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
	EXPECT_TRUE(DrawDirect());
	ExpectCallerState();
	EXPECT_EQ(Graphics().m_Buffers.size(), Buffers);
	EXPECT_EQ(Graphics().m_Containers.size(), Containers);
}
