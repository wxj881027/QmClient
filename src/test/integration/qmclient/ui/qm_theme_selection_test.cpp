// 主题名称绘制与设置卡片括号说明作用域协作；仅隔离标签提交边界。
#include <game/client/QmUi/ThemeSelectionLayout.h>
#include <game/client/components/theme_scan.h>
#include <gtest/gtest.h>
#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmThemeSelection : public qm_ui_test::CRealUiFixture
	{
	protected:
		const CUIRect m_Row{0, 0, 200, 30};
		uint64_t Draw(const char *pName, bool ThemeLabel)
		{
			auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
			pGraphics->ResetCounters();
			if(ThemeLabel)
				EXPECT_FALSE(DrawThemeSelectionLabel(&m_Ui, m_Row, pName, 14.0f).m_Truncated);
			else
				m_Ui.DoLabel(&m_Row, pName, 14.0f, TEXTALIGN_ML);
			return pGraphics->m_Quads;
		}
	};
}

TEST_F(QmThemeSelection, BuiltinsKeepTheirFullGlyphsInsideCardHintScope)
{
	for(const char *pName : {"", "auto", "rand"})
	{
		const std::string Name = ThemeDisplayName(pName);
		m_Ui.SetCardLabelHintsEnabled(false);
		const uint64_t LiteralQuads = Draw(Name.c_str(), false);
		ASSERT_GT(LiteralQuads, 0u);
		m_Ui.SetCardLabelHintsEnabled(true);
		EXPECT_STREQ(m_Ui.PrepareCardLabel(&m_Row, Name.c_str(), false), "…");
		EXPECT_LT(Draw(Name.c_str(), false), LiteralQuads);
		EXPECT_EQ(Draw(Name.c_str(), true), LiteralQuads);
		EXPECT_TRUE(m_Ui.CardLabelHintsEnabled());
	}
}

TEST_F(QmThemeSelection, CustomNameParenthesesStayLiteralAndRestoreCallerScope)
{
	const char *pName = "autumn (night)";
	m_Ui.SetCardLabelHintsEnabled(false);
	const uint64_t LiteralQuads = Draw(pName, false);
	m_Ui.SetCardLabelHintsEnabled(true);
	EXPECT_EQ(Draw(pName, true), LiteralQuads);
	EXPECT_TRUE(m_Ui.CardLabelHintsEnabled());
	m_Ui.SetCardLabelHintsEnabled(false);
	EXPECT_EQ(Draw(pName, true), LiteralQuads);
	EXPECT_FALSE(m_Ui.CardLabelHintsEnabled());
}

TEST_F(QmThemeSelection, EmptyRowsDoNotSubmitUnboundedText)
{
	auto *pGraphics = static_cast<qm_ui_test::CRealUiTestGraphics *>(m_Ui.Graphics());
	pGraphics->ResetCounters();
	DrawThemeSelectionLabel(&m_Ui, {0, 0, 0, 30}, "(none)", 14.0f);
	DrawThemeSelectionLabel(&m_Ui, {0, 0, 100, 0}, "(seasons)", 14.0f);
	EXPECT_EQ(pGraphics->m_Quads, 0u);
}

TEST(ThemeSelection, NarrowRowsPreserveNameWidthBeforeAllocatingPreview)
{
	const CUIRect Row{10, 20, 90, 40};
	const auto Layout = ResolveThemeSelectionRowLayout(Row, 70.0f, 4.0f);
	EXPECT_GE(Layout.m_Label.w, 70.0f);
	EXPECT_GT(Layout.m_Icon.w, 0.0f);
	EXPECT_LT(Layout.m_Icon.w, Row.h * 2.0f);
	EXPECT_FLOAT_EQ(Layout.m_Label.x + Layout.m_Label.w, Row.x + Row.w);
	const auto TooNarrow = ResolveThemeSelectionRowLayout({10, 20, 50, 40}, 70.0f, 4.0f);
	EXPECT_FLOAT_EQ(TooNarrow.m_Icon.w, 0.0f);
	EXPECT_FLOAT_EQ(TooNarrow.m_Label.w, 50.0f);
	const auto Empty = ResolveThemeSelectionRowLayout({0, 0, 0, 40}, 70.0f, 4.0f);
	EXPECT_FLOAT_EQ(Empty.m_Label.w, 0.0f);
	EXPECT_FLOAT_EQ(Empty.m_Icon.w, 0.0f);
}
