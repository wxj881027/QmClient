// 真实 UI 标签与字体字面、缓存失效协作；仅隔离 GPU 设备。
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
