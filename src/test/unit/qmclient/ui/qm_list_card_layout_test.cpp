#include <game/client/QmUi/QmListCard.h>

#include <gtest/gtest.h>

TEST(QmListCardLayout, ContentIsInsetByPaddingOnAllSides)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 14.0f;
	const CUIRect Card{100.0f, 50.0f, 200.0f, 160.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), {});
	EXPECT_FLOAT_EQ(Layout.m_Content.x, 114.0f);
	EXPECT_FLOAT_EQ(Layout.m_Content.y, 64.0f);
	EXPECT_FLOAT_EQ(Layout.m_Content.w, 172.0f);
	EXPECT_FLOAT_EQ(Layout.m_Content.h, 132.0f);
}

TEST(QmListCardLayout, BodyStartsBelowHeaderAndReachesContentBottomWithoutFooter)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 10.0f;
	Metrics.m_TitleHeight = 20.0f;
	Metrics.m_HeaderGap = 8.0f;
	const CUIRect Card{0.0f, 0.0f, 200.0f, 160.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), {});
	EXPECT_FLOAT_EQ(Layout.m_Title.h, 20.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.y, Layout.m_Title.y + 20.0f + 8.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.y + Layout.m_Body.h, Layout.m_Content.y + Layout.m_Content.h);
	EXPECT_FLOAT_EQ(Layout.m_Footer.w, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Footer.h, 0.0f);
}

TEST(QmListCardLayout, FooterTakesBottomOfContentAndLeavesGapAboveBody)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 10.0f;
	Metrics.m_TitleHeight = 20.0f;
	Metrics.m_HeaderGap = 8.0f;
	Metrics.m_FooterGap = 6.0f;
	qm_list_card::SOptions Options;
	Options.m_FooterHeight = 30.0f;
	const CUIRect Card{0.0f, 0.0f, 200.0f, 160.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), Options);
	EXPECT_FLOAT_EQ(Layout.m_Footer.h, 30.0f);
	EXPECT_FLOAT_EQ(Layout.m_Footer.y + Layout.m_Footer.h, Layout.m_Content.y + Layout.m_Content.h);
	EXPECT_FLOAT_EQ(Layout.m_Body.y + Layout.m_Body.h, Layout.m_Footer.y - 6.0f);
}

TEST(QmListCardLayout, TrailingReservesRightColumnAndShrinksTitleRow)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 10.0f;
	Metrics.m_TitleHeight = 20.0f;
	qm_list_card::SOptions Options;
	Options.m_TrailingWidth = 24.0f;
	const CUIRect Card{0.0f, 0.0f, 200.0f, 160.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), Options);
	EXPECT_FLOAT_EQ(Layout.m_Trailing.w, 24.0f);
	EXPECT_FLOAT_EQ(Layout.m_Trailing.x + Layout.m_Trailing.w, Layout.m_Content.x + Layout.m_Content.w);
	EXPECT_FLOAT_EQ(Layout.m_Title.w, Layout.m_Content.w - 24.0f - ui_token::spacing::XS);
	// 尾部只占标题行时，它的高度不越过标题行。
	EXPECT_FLOAT_EQ(Layout.m_Trailing.h, 20.0f);
}

TEST(QmListCardLayout, SubtitleRowSitsUnderTitleInsideHeader)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 10.0f;
	Metrics.m_TitleHeight = 20.0f;
	Metrics.m_SubtitleHeight = 12.0f;
	Metrics.m_HeaderGap = 8.0f;
	qm_list_card::SOptions Options;
	Options.m_HasSubtitle = true;
	const CUIRect Card{0.0f, 0.0f, 200.0f, 160.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), Options);
	EXPECT_FLOAT_EQ(Layout.m_Header.h, 32.0f);
	EXPECT_FLOAT_EQ(Layout.m_Subtitle.y, Layout.m_Title.y + 20.0f);
	EXPECT_FLOAT_EQ(Layout.m_Subtitle.h, 12.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.y, Layout.m_Header.y + 32.0f + 8.0f);
}

TEST(QmListCardLayout, ScaleMultipliesPaddingRadiusAndRows)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_UiScale = 2.0f;
	Metrics.m_Padding = 10.0f;
	Metrics.m_Radius = 14.0f;
	Metrics.m_TitleHeight = 20.0f;
	const CUIRect Card{0.0f, 0.0f, 400.0f, 320.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), {});
	EXPECT_FLOAT_EQ(Layout.m_Content.x, 20.0f);
	EXPECT_FLOAT_EQ(Layout.m_Content.w, 360.0f);
	EXPECT_FLOAT_EQ(Layout.m_Radius, 28.0f);
	EXPECT_FLOAT_EQ(Layout.m_Title.h, 40.0f);
}

TEST(QmListCardLayout, ShortCardKeepsBodyNonNegative)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 10.0f;
	Metrics.m_TitleHeight = 20.0f;
	Metrics.m_HeaderGap = 8.0f;
	qm_list_card::SOptions Options;
	Options.m_FooterHeight = 40.0f;
	const CUIRect Card{0.0f, 0.0f, 200.0f, 30.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), Options);
	EXPECT_GE(Layout.m_Content.h, 0.0f);
	EXPECT_GE(Layout.m_Body.h, 0.0f);
	EXPECT_LE(Layout.m_Footer.h, Layout.m_Content.h);
}

TEST(QmListCardLayout, RowAxisSplitsLeadingFromBodyWithoutPadding)
{
	// 皮肤格：整格当作卡片，内边距 0，左侧 60 宽预览，右侧是名称与图标区。
	qm_list_card::SMetrics Metrics;
	Metrics.m_UiScale = 1.0f;
	Metrics.m_Padding = 0.0f;
	Metrics.m_Radius = 0.0f;
	qm_list_card::SOptions Options;
	Options.m_Axis = qm_list_card::EAxis::ROW;
	Options.m_LeadingWidth = 60.0f;
	Options.m_RowGap = 0.0f;
	const CUIRect Card{100.0f, 40.0f, 200.0f, 50.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), Options);
	EXPECT_FLOAT_EQ(Layout.m_Content.x, Card.x);
	EXPECT_FLOAT_EQ(Layout.m_Content.h, Card.h);
	EXPECT_FLOAT_EQ(Layout.m_Leading.x, Card.x);
	EXPECT_FLOAT_EQ(Layout.m_Leading.w, 60.0f);
	EXPECT_FLOAT_EQ(Layout.m_Leading.h, Card.h);
	EXPECT_FLOAT_EQ(Layout.m_Body.x, Card.x + 60.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.w, Card.w - 60.0f);
	// ROW 形态没有头部/底栏，竖向切分交给调用点。
	EXPECT_FLOAT_EQ(Layout.m_Header.h, 0.0f);
	EXPECT_FLOAT_EQ(Layout.m_Footer.h, 0.0f);
}

TEST(QmListCardLayout, RowAxisReservesGapAndTrailingSlot)
{
	qm_list_card::SMetrics Metrics;
	Metrics.m_Padding = 8.0f;
	qm_list_card::SOptions Options;
	Options.m_Axis = qm_list_card::EAxis::ROW;
	Options.m_LeadingWidth = 40.0f;
	Options.m_TrailingSlotWidth = 20.0f;
	Options.m_RowGap = 4.0f;
	const CUIRect Card{0.0f, 0.0f, 180.0f, 40.0f};
	const qm_list_card::SLayout Layout = qm_list_card::ResolveLayout(Card, Metrics.Scaled(), Options);
	EXPECT_FLOAT_EQ(Layout.m_Content.x, 8.0f);
	EXPECT_FLOAT_EQ(Layout.m_Content.w, 164.0f);
	EXPECT_FLOAT_EQ(Layout.m_Leading.w, 40.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.x, 8.0f + 40.0f + 4.0f);
	EXPECT_FLOAT_EQ(Layout.m_TrailingSlot.x + Layout.m_TrailingSlot.w, 8.0f + 164.0f);
	EXPECT_FLOAT_EQ(Layout.m_TrailingSlot.w, 20.0f);
	EXPECT_FLOAT_EQ(Layout.m_Body.w, 164.0f - 40.0f - 4.0f - 20.0f - 4.0f);
}

