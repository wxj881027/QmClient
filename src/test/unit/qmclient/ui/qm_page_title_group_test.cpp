#include <game/client/QmUi/UiTokens.h>

#include <gtest/gtest.h>

namespace
{
	float GroupCenterX(const ui_token::page_title::STitleGroupLayout &Layout)
	{
		return (Layout.m_Icon.x + Layout.m_Text.x + Layout.m_Text.w) * 0.5f;
	}
}

TEST(QmPageTitleGroup, GroupCenterMatchesRowCenter)
{
	const CUIRect Row{100.0f, 20.0f, 300.0f, ui_token::page_title::HEIGHT};
	const auto Layout = ui_token::page_title::ResolveTitleGroup(Row, 60.0f);
	// 文字单独按整行居中时，这个断言会偏左 (图标宽 + 缝)/2。
	EXPECT_NEAR(GroupCenterX(Layout), Row.Center().x, 0.001f);
}

TEST(QmPageTitleGroup, IconAndTextShareGapAndRowHeight)
{
	const CUIRect Row{0.0f, 50.0f, 240.0f, ui_token::page_title::HEIGHT};
	const auto Layout = ui_token::page_title::ResolveTitleGroup(Row, 55.0f);
	EXPECT_FLOAT_EQ(Layout.m_Text.x - (Layout.m_Icon.x + Layout.m_Icon.w), ui_token::page_title::ICON_GAP);
	EXPECT_FLOAT_EQ(Layout.m_Icon.w, ui_token::page_title::ICON_SIZE);
	EXPECT_FLOAT_EQ(Layout.m_Icon.h, ui_token::page_title::ICON_SIZE);
	EXPECT_FLOAT_EQ(Layout.m_Text.h, Row.h);
	// 垂直方向仍按 token 做视觉下沉，别被水平居中改动顺手抹掉。
	EXPECT_FLOAT_EQ(Layout.m_Icon.y, Row.y + (Row.h - ui_token::page_title::ICON_SIZE) * 0.5f + ui_token::page_title::ICON_Y_OFFSET);
}

TEST(QmPageTitleGroup, TextRectKeepsNaturalWidthSoRendererNeverBreaks)
{
	// 行宽不够时也不能裁窄文字矩形：中文没有空格，裁窄就等于让渲染器在字间断行，
	// 「设置」会变成上下两行。宁可整组居中溢出到两侧空白，字号与单行都不变。
	const CUIRect Row{0.0f, 0.0f, 100.0f, ui_token::page_title::HEIGHT};
	const auto Layout = ui_token::page_title::ResolveTitleGroup(Row, 500.0f);
	EXPECT_FLOAT_EQ(Layout.m_Text.w, 500.0f);
	EXPECT_NEAR(GroupCenterX(Layout), Row.Center().x, 0.001f);
}
