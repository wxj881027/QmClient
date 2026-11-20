#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_LIST_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_RANK_LIST_LAYOUT_H

#include <algorithm>

// Rank 1 列表的列宽分配。
//
// 固定列（激活标识 / 模式 / 时间 / 日期）按内容宽度申请，剩余宽度按比例分给
// 玩家名与状态列。窗口窄到连固定列都放不下时整体等比压缩，绝不产生负宽度；
// 纯几何计算，不依赖文本渲染或 UI 状态，便于独立测试与极端尺寸回归。
namespace qmclient::rank_demo
{
	enum class ERankListColumn
	{
		ACTIVE = 0,
		MODE,
		NAME,
		TIME,
		DATE,
		STATE,
		COUNT,
	};

	inline constexpr int RANK_LIST_COLUMN_COUNT = static_cast<int>(ERankListColumn::COUNT);

	struct SRankListColumnMetrics
	{
		// 表头内容区的可用总宽度（已扣掉滚动条让位与行内退）。
		float m_AvailableWidth = 0.0f;
		// 相邻两列之间的间距。
		float m_Gap = 0.0f;
		// 固定列的期望宽度；Mode/Time/Date 含单元格左右内边距。
		float m_ActiveWidth = 0.0f;
		float m_ModeWidth = 0.0f;
		float m_TimeWidth = 0.0f;
		float m_DateWidth = 0.0f;
		// 剩余宽度给玩家列的份额（0~1），其余给状态列。
		float m_NameShare = 0.6f;
	};

	struct SRankListColumns
	{
		float m_aWidths[RANK_LIST_COLUMN_COUNT] = {};

		float operator[](ERankListColumn Column) const { return m_aWidths[static_cast<int>(Column)]; }
		float &operator[](ERankListColumn Column) { return m_aWidths[static_cast<int>(Column)]; }
	};

	inline SRankListColumns LayoutRankListColumns(const SRankListColumnMetrics &Metrics)
	{
		SRankListColumns Columns;
		const float ColumnGap = std::max(0.0f, Metrics.m_Gap);
		const float GapTotal = ColumnGap * (RANK_LIST_COLUMN_COUNT - 1);
		const float Available = std::max(0.0f, Metrics.m_AvailableWidth - GapTotal);

		float aFixed[4] = {
			std::max(0.0f, Metrics.m_ActiveWidth),
			std::max(0.0f, Metrics.m_ModeWidth),
			std::max(0.0f, Metrics.m_TimeWidth),
			std::max(0.0f, Metrics.m_DateWidth),
		};
		float FixedTotal = aFixed[0] + aFixed[1] + aFixed[2] + aFixed[3];
		if(FixedTotal > Available && FixedTotal > 0.0f)
		{
			// 只压缩固定列：比例不变，玩家与状态列没有剩余空间可让。
			const float Scale = Available / FixedTotal;
			for(float &Width : aFixed)
				Width *= Scale;
			FixedTotal = Available;
		}

		Columns[ERankListColumn::ACTIVE] = aFixed[0];
		Columns[ERankListColumn::MODE] = aFixed[1];
		Columns[ERankListColumn::TIME] = aFixed[2];
		Columns[ERankListColumn::DATE] = aFixed[3];
		const float Flex = std::max(0.0f, Available - FixedTotal);
		const float Share = std::clamp(Metrics.m_NameShare, 0.0f, 1.0f);
		Columns[ERankListColumn::NAME] = Flex * Share;
		Columns[ERankListColumn::STATE] = Flex - Columns[ERankListColumn::NAME];
		return Columns;
	}
} // namespace qmclient::rank_demo

#endif
