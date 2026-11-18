// 表面文字作用域与真实文字引擎协作，验证描边及父层透明度。
#include <game/client/QmUi/UiSurfaceText.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmSurfaceText : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUIElement::SUIElementRect m_Label;
		const CUIRect m_Rect{0, 0, 320, 24};

		void TearDown() override
		{
			m_Ui.TextRender()->DeleteTextContainer(m_Label.m_UITextContainer);
			CRealUiFixture::TearDown();
		}
	};
}

TEST_F(QmSurfaceText, StreamedBrowserLabelsKeepDefaultOutlineOpacity)
{
	auto *pTextRender = m_Ui.TextRender();
	struct SRestoreConfig
	{
		CConfig m_Previous = g_Config;
		~SRestoreConfig() { g_Config = m_Previous; }
	} Restore;
	// 0 = 白色：文本颜色模式四选一的第一档。
	g_Config.m_QmUiTextColorMode = 0;
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->TextOutlineColor(pTextRender->DefaultTextOutlineColor());
	SLabelProperties Props;
	Props.m_MaxWidth = m_Rect.w;
	Props.m_StopAtEnd = true;
	Props.m_EnableWidthCheck = false;
	{
		CUiScopedSurfaceText Surface(pTextRender, ColorRGBA(0.4f, 0.4f, 0.4f, 0.8f));
		m_Ui.DoLabelStreamed(m_Label, &m_Rect, "DDNet CHN6 - Novice", 12.0f, TEXTALIGN_ML, Props);
		ASSERT_TRUE(m_Label.m_UITextContainer.Valid());
		EXPECT_EQ(m_Label.m_TextColor, ColorRGBA(1, 1, 1, 1));
		EXPECT_EQ(m_Label.m_TextOutlineColor, ColorRGBA(0, 0, 0, 0.3f));

		pTextRender->TextColor(ColorRGBA(0.4f, 0.7f, 1.0f, 1.0f));
		m_Ui.DoLabelStreamed(m_Label, &m_Rect, "DDRaceNetwork", 12.0f, TEXTALIGN_ML, Props);
		EXPECT_EQ(m_Label.m_TextColor, ColorRGBA(0.4f, 0.7f, 1.0f, 1.0f));
		EXPECT_EQ(m_Label.m_TextOutlineColor, ColorRGBA(0, 0, 0, 0.3f));
	}
	EXPECT_EQ(pTextRender->GetTextColor(), pTextRender->DefaultTextColor());
	EXPECT_EQ(pTextRender->GetTextOutlineColor(), pTextRender->DefaultTextOutlineColor());
}

TEST_F(QmSurfaceText, NestedSurfacesPreserveCallerOpacityAndRestoreColors)
{
	auto *pTextRender = m_Ui.TextRender();
	struct SRestoreConfig
	{
		CConfig m_Previous = g_Config;
		~SRestoreConfig() { g_Config = m_Previous; }
	} Restore;
	g_Config.m_QmUiTextColorMode = 0;
	const ColorRGBA CallerText(0.2f, 0.3f, 0.4f, 0.4f);
	const ColorRGBA CallerOutline(0.8f, 0.7f, 0.6f, 0.12f);
	pTextRender->TextColor(CallerText);
	pTextRender->TextOutlineColor(CallerOutline);
	{
		CUiScopedSurfaceText Parent(pTextRender, ColorRGBA(0, 0, 0, 1));
		EXPECT_EQ(pTextRender->GetTextColor(), ColorRGBA(1, 1, 1, 0.4f));
		EXPECT_EQ(pTextRender->GetTextOutlineColor(), ColorRGBA(0, 0, 0, 0.12f));
		{
			CUiScopedSurfaceText Child(pTextRender, ColorRGBA(1, 1, 1, 1));
			// 文本颜色是显式模式，不再按嵌套表面明暗翻转；调用方透明度继续跟随。
			EXPECT_EQ(pTextRender->GetTextColor(), ColorRGBA(1, 1, 1, 0.4f));
			EXPECT_EQ(pTextRender->GetTextOutlineColor(), ColorRGBA(0, 0, 0, 0.12f));
			// 切到黑色模式后，嵌套表面也不影响结果：黑字配白描边。
			g_Config.m_QmUiTextColorMode = 1;
			const CUiScopedSurfaceText BlackOnLight(pTextRender, ColorRGBA(1, 1, 1, 1));
			EXPECT_EQ(pTextRender->GetTextColor(), ColorRGBA(0, 0, 0, 0.4f));
			EXPECT_EQ(pTextRender->GetTextOutlineColor(), ColorRGBA(1, 1, 1, 0.12f));
		}
		EXPECT_EQ(pTextRender->GetTextColor(), ColorRGBA(1, 1, 1, 0.4f));
		EXPECT_EQ(pTextRender->GetTextOutlineColor(), ColorRGBA(0, 0, 0, 0.12f));
	}
	EXPECT_EQ(pTextRender->GetTextColor(), CallerText);
	EXPECT_EQ(pTextRender->GetTextOutlineColor(), CallerOutline);
}
