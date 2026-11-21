#include <game/client/QmUi/QmListToolbar.h>
#include <game/client/components/qmclient/rank_list_layout.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace
{
	using qmclient::rank_demo::ERankListColumn;
	using qmclient::rank_demo::LayoutRankListColumns;
	using qmclient::rank_demo::SRankListColumnMetrics;

	float TotalWidth(const qmclient::rank_demo::SRankListColumns &Columns, float Gap)
	{
		float Total = 0.0f;
		for(int Column = 0; Column < qmclient::rank_demo::RANK_LIST_COLUMN_COUNT; Column++)
			Total += Columns.m_aWidths[Column];
		return Total + Gap * (qmclient::rank_demo::RANK_LIST_COLUMN_COUNT - 1);
	}

	void ExpectNoNegativeWidths(const qmclient::rank_demo::SRankListColumns &Columns)
	{
		for(int Column = 0; Column < qmclient::rank_demo::RANK_LIST_COLUMN_COUNT; Column++)
			EXPECT_GE(Columns.m_aWidths[Column], 0.0f) << "column " << Column;
	}
} // namespace

TEST(QmRankListLayout, WideRowGivesNameSixtyPercentOfRemainingWidth)
{
	SRankListColumnMetrics Metrics;
	Metrics.m_AvailableWidth = 1000.0f;
	Metrics.m_Gap = 10.0f;
	Metrics.m_ActiveWidth = 20.0f;
	Metrics.m_ModeWidth = 100.0f;
	Metrics.m_TimeWidth = 100.0f;
	Metrics.m_DateWidth = 100.0f;
	Metrics.m_NameShare = 0.6f;

	const auto Columns = LayoutRankListColumns(Metrics);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::ACTIVE], 20.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::MODE], 100.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::TIME], 100.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::DATE], 100.0f);
	// 可用宽度 1000 - 5 个间距 = 950；固定列 320，剩余 630 按 6:4 分给玩家与状态。
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::NAME], 378.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::STATE], 252.0f);
	EXPECT_FLOAT_EQ(TotalWidth(Columns, Metrics.m_Gap), Metrics.m_AvailableWidth);
}

TEST(QmRankListLayout, TightRowScalesOnlyFixedColumnsAndStarvesFlex)
{
	SRankListColumnMetrics Metrics;
	Metrics.m_AvailableWidth = 100.0f;
	Metrics.m_Gap = 10.0f;
	Metrics.m_ActiveWidth = 20.0f;
	Metrics.m_ModeWidth = 100.0f;
	Metrics.m_TimeWidth = 100.0f;
	Metrics.m_DateWidth = 100.0f;

	const auto Columns = LayoutRankListColumns(Metrics);
	// 扣掉间距只剩 50，固定列按比例压缩到总和 50，玩家与状态列没有空间。
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::ACTIVE], 50.0f * 20.0f / 320.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::MODE], 50.0f * 100.0f / 320.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::TIME], 50.0f * 100.0f / 320.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::DATE], 50.0f * 100.0f / 320.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::NAME], 0.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::STATE], 0.0f);
	ExpectNoNegativeWidths(Columns);
}

TEST(QmRankListLayout, ExtremeMetricsNeverProduceNegativeWidths)
{
	const std::array<float, 6> aWidths = {0.0f, -50.0f, 10.0f, 15.0f, 40.0f, 100.0f};
	const std::array<float, 4> aGaps = {-10.0f, 0.0f, 10.0f, 500.0f};
	const std::array<float, 4> aShares = {-3.0f, 0.0f, 0.6f, 5.0f};
	for(const float Width : aWidths)
	{
		for(const float Gap : aGaps)
		{
			for(const float Share : aShares)
			{
				SRankListColumnMetrics Metrics;
				Metrics.m_AvailableWidth = Width;
				Metrics.m_Gap = Gap;
				Metrics.m_ActiveWidth = 20.0f;
				Metrics.m_ModeWidth = 100.0f;
				Metrics.m_TimeWidth = 100.0f;
				Metrics.m_DateWidth = 100.0f;
				Metrics.m_NameShare = Share;
				ExpectNoNegativeWidths(LayoutRankListColumns(Metrics));
			}
		}
	}
}

TEST(QmRankListLayout, NameShareIsClampedToUnitInterval)
{
	SRankListColumnMetrics Metrics;
	Metrics.m_AvailableWidth = 500.0f;
	Metrics.m_Gap = 0.0f;
	Metrics.m_NameShare = 5.0f;
	auto Columns = LayoutRankListColumns(Metrics);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::STATE], 0.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::NAME], 500.0f);

	Metrics.m_NameShare = -5.0f;
	Columns = LayoutRankListColumns(Metrics);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::NAME], 0.0f);
	EXPECT_FLOAT_EQ(Columns[ERankListColumn::STATE], 500.0f);
}

TEST(QmRankListLayout, ToolbarPlacesSearchLeftOfRefreshAndFolderRightmost)
{
	CUIRect Row = {0.0f, 0.0f, 600.0f, 40.0f};
	const auto Layout = ui_widget::LayoutListToolbar(Row, ui_token::font::BODY);

	EXPECT_FLOAT_EQ(Layout.m_Height, ui_token::font::BODY + 2.0f * ui_token::spacing::SM);
	// 两个圆形按钮宽度等于工具栏高度，目录按钮贴右边缘。
	EXPECT_FLOAT_EQ(Layout.m_FolderRect.w, Layout.m_Height);
	EXPECT_FLOAT_EQ(Layout.m_FolderRect.x + Layout.m_FolderRect.w, Row.x + Row.w);
	EXPECT_FLOAT_EQ(Layout.m_RefreshRect.w, Layout.m_Height);
	EXPECT_FLOAT_EQ(Layout.m_RefreshRect.x + Layout.m_RefreshRect.w, Layout.m_FolderRect.x - ui_token::spacing::XS);
	EXPECT_FLOAT_EQ(Layout.m_SearchRect.x, Row.x);
	EXPECT_FLOAT_EQ(Layout.m_SearchRect.x + Layout.m_SearchRect.w, Layout.m_RefreshRect.x - ui_token::spacing::XS);
	EXPECT_GT(Layout.m_SearchRect.w, 0.0f);
}

TEST(QmRankListLayout, ToolbarClampsWhenRowIsNarrowerThanButtons)
{
	const std::array<float, 4> aWidths = {0.0f, 1.0f, 5.0f, 20.0f};
	for(const float Width : aWidths)
	{
		CUIRect Row = {0.0f, 0.0f, Width, 40.0f};
		const auto Layout = ui_widget::LayoutListToolbar(Row, ui_token::font::BODY);
		EXPECT_GE(Layout.m_SearchRect.w, 0.0f);
		EXPECT_GE(Layout.m_RefreshRect.w, 0.0f);
		EXPECT_GE(Layout.m_FolderRect.w, 0.0f);
		EXPECT_LE(Layout.m_FolderRect.x + Layout.m_FolderRect.w, Row.x + Row.w);
		EXPECT_LE(Layout.m_SearchRect.x + Layout.m_SearchRect.w, Row.x + Row.w);
	}
}

TEST(QmRankListLayout, ToolbarHeightClampsToShortRow)
{
	CUIRect Row = {0.0f, 0.0f, 300.0f, 2.0f};
	const auto Layout = ui_widget::LayoutListToolbar(Row, ui_token::font::BODY);
	EXPECT_FLOAT_EQ(Layout.m_SearchRect.h, 2.0f);
	EXPECT_FLOAT_EQ(Layout.m_RefreshRect.h, 2.0f);
	EXPECT_FLOAT_EQ(Layout.m_FolderRect.h, 2.0f);
}
