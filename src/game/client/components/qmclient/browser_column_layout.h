#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

struct SQmBrowserColumnSizing
{
	float m_Width;
	float m_MinWidth;
	float m_MaxWidth;
	float m_PreferredWidth;
};

namespace qm_browser_column_detail
{
	template<size_t NumColumns>
	inline double TotalWidth(const std::array<SQmBrowserColumnSizing, NumColumns> &aColumns, size_t First)
	{
		double Total = 0.0;
		for(size_t i = First; i < NumColumns; ++i)
			Total += aColumns[i].m_Width;
		return Total;
	}

	// 浮点落回列宽时消化舍入尾差；不触及 First 左侧，也不突破有效边界。
	template<size_t NumColumns>
	inline void CorrectRounding(std::array<SQmBrowserColumnSizing, NumColumns> &aColumns, size_t First, double Target)
	{
		double Remaining = Target - TotalWidth(aColumns, First);
		for(size_t i = First; i < NumColumns && Remaining != 0.0; ++i)
		{
			auto &Column = aColumns[i];
			const float Width = (float)std::clamp((double)Column.m_Width + Remaining, (double)Column.m_MinWidth, (double)Column.m_MaxWidth);
			Remaining -= (double)Width - Column.m_Width;
			Column.m_Width = Width;
		}
	}

	// 按 Width - PreferredWidth 的水位分配：收窄先取富余最多的列，
	// 补宽先给最拥挤的列；到达同一水位后共同分担，到上下限后退出。
	template<size_t NumColumns>
	inline void Distribute(std::array<SQmBrowserColumnSizing, NumColumns> &aColumns, size_t First, double Delta)
	{
		if(First >= NumColumns || Delta == 0.0)
			return;
		const bool Grow = Delta > 0.0;
		double Capacity = 0.0;
		double Low = std::numeric_limits<double>::max();
		double High = std::numeric_limits<double>::lowest();
		for(size_t i = First; i < NumColumns; ++i)
		{
			const auto &Column = aColumns[i];
			Capacity += Grow ? (double)Column.m_MaxWidth - Column.m_Width : (double)Column.m_Width - Column.m_MinWidth;
			Low = std::min(Low, (double)(Grow ? Column.m_Width : Column.m_MinWidth) - Column.m_PreferredWidth);
			High = std::max(High, (double)(Grow ? Column.m_MaxWidth : Column.m_Width) - Column.m_PreferredWidth);
		}
		if(Capacity <= 0.0)
			return;
		const double Amount = std::min(Grow ? Delta : -Delta, Capacity);
		const double Target = TotalWidth(aColumns, First) + (Grow ? Amount : -Amount);
		if(Amount == Capacity)
		{
			for(size_t i = First; i < NumColumns; ++i)
				aColumns[i].m_Width = Grow ? aColumns[i].m_MaxWidth : aColumns[i].m_MinWidth;
			return;
		}

		// 迭代次数与像素距离无关，小数组直接栈上求解，不排序也不分配。
		for(int Step = 0; Step < 48; ++Step)
		{
			const double Level = Low + (High - Low) * 0.5;
			double Sum = 0.0;
			for(size_t i = First; i < NumColumns; ++i)
			{
				const auto &Column = aColumns[i];
				const double Min = Grow ? Column.m_Width : Column.m_MinWidth;
				const double Max = Grow ? Column.m_MaxWidth : Column.m_Width;
				Sum += std::clamp((double)Column.m_PreferredWidth + Level, Min, Max);
			}
			if(Sum < Target)
				Low = Level;
			else
				High = Level;
		}
		const double Level = Low + (High - Low) * 0.5;
		for(size_t i = First; i < NumColumns; ++i)
		{
			auto &Column = aColumns[i];
			const double Min = Grow ? Column.m_Width : Column.m_MinWidth;
			const double Max = Grow ? Column.m_MaxWidth : Column.m_Width;
			Column.m_Width = (float)std::clamp((double)Column.m_PreferredWidth + Level, Min, Max);
		}
		CorrectRounding(aColumns, First, Target);
	}
}

// AvailableWidth 只包含列宽预算；列间距由调用方预先扣除。
// 返回本视口的有效边界，调用方保留原配置边界供下一次窗口变化重新拟合。
template<size_t NumColumns>
inline std::array<SQmBrowserColumnSizing, NumColumns> QmBrowserFitColumns(std::array<SQmBrowserColumnSizing, NumColumns> aColumns, float AvailableWidth)
{
	const double Budget = std::max(0.0f, AvailableWidth);
	double MinTotal = 0.0;
	double MaxTotal = 0.0;
	size_t FlexibleColumns = 0;
	for(auto &Column : aColumns)
	{
		Column.m_MinWidth = std::max(0.0f, Column.m_MinWidth);
		Column.m_MaxWidth = std::max(Column.m_MinWidth, Column.m_MaxWidth);
		Column.m_Width = std::clamp(Column.m_Width, Column.m_MinWidth, Column.m_MaxWidth);
		MinTotal += Column.m_MinWidth;
		MaxTotal += Column.m_MaxWidth;
		FlexibleColumns += Column.m_MaxWidth > Column.m_MinWidth ? 1 : 0;
	}
	if(Budget < MinTotal)
	{
		// 极窄视口连最小宽度也放不下时只缩列盒，不缩字号；所有列锁在有效下限。
		const double Scale = Budget / MinTotal;
		for(auto &Column : aColumns)
		{
			const bool Fixed = Column.m_MinWidth == Column.m_MaxWidth;
			Column.m_Width = Column.m_MinWidth = (float)(Column.m_MinWidth * Scale);
			if(Fixed)
				Column.m_MaxWidth = Column.m_MinWidth;
		}
		return aColumns;
	}
	if(Budget > MaxTotal && FlexibleColumns > 0)
	{
		// 极宽视口只临时扩展可调内容列的上限，固定图标仍保持原尺寸。
		const double Extra = Budget - MaxTotal;
		for(auto &Column : aColumns)
			if(Column.m_MaxWidth > Column.m_MinWidth)
				Column.m_MaxWidth = (float)((double)Column.m_MaxWidth + Extra);
	}
	// 全部为固定图标且预算更宽时保留右侧空白，不能为填满窗口拉伸图标。
	qm_browser_column_detail::Distribute(aColumns, 0, Budget - qm_browser_column_detail::TotalWidth(aColumns, 0));
	return aColumns;
}

// 每一帧从按下时的有效列宽快照与累计鼠标位移计算，不累计上一帧的舍入。
// 只有右侧可调列参与补偿，左侧宽度和总预算保持不变；最右列没有调宽边界。
template<size_t NumColumns>
inline std::array<SQmBrowserColumnSizing, NumColumns> QmBrowserResizeColumns(const std::array<SQmBrowserColumnSizing, NumColumns> &aStart, size_t ColumnIndex, float Delta)
{
	auto aColumns = aStart;
	if(ColumnIndex >= NumColumns || ColumnIndex + 1 >= NumColumns || Delta == 0.0f)
		return aColumns;
	// 拖动只让出内容之外的空白；原本显示不全的列停在按下时的宽度。
	for(size_t i = ColumnIndex; i < NumColumns; ++i)
		aColumns[i].m_MinWidth = std::max(aStart[i].m_MinWidth, std::min(aStart[i].m_Width, aStart[i].m_PreferredWidth));
	auto &Column = aColumns[ColumnIndex];
	const double Change = std::clamp((double)Delta, (double)Column.m_MinWidth - Column.m_Width, (double)Column.m_MaxWidth - Column.m_Width);
	const double RightBefore = qm_browser_column_detail::TotalWidth(aColumns, ColumnIndex + 1);
	const double Total = Column.m_Width + RightBefore;
	qm_browser_column_detail::Distribute(aColumns, ColumnIndex + 1, -Change);
	Column.m_Width = (float)std::clamp(Total - qm_browser_column_detail::TotalWidth(aColumns, ColumnIndex + 1), (double)Column.m_MinWidth, (double)Column.m_MaxWidth);
	qm_browser_column_detail::CorrectRounding(aColumns, ColumnIndex + 1, Total - Column.m_Width);
	// 内容下限只属于本次按压，不能写入布局缓存后限制下一次内容测量。
	for(size_t i = ColumnIndex; i < NumColumns; ++i)
		aColumns[i].m_MinWidth = aStart[i].m_MinWidth;
	return aColumns;
}

// 表头共享一次按压的所有权；列边缘命中时调宽优先，直到释放不能换成另一种手势。
class CQmBrowserHeaderGesture
{
	int m_ReorderColumn = -1;
	int m_ResizeColumn = -1;
	bool m_ReorderDragged = false;

public:
	void Begin(int HeaderColumn, int ResizeColumn)
	{
		if(m_ReorderColumn >= 0 || m_ResizeColumn >= 0)
			return;
		m_ReorderDragged = false;
		m_ResizeColumn = ResizeColumn;
		m_ReorderColumn = ResizeColumn >= 0 ? -1 : HeaderColumn;
	}
	void End()
	{
		m_ReorderColumn = -1;
		m_ResizeColumn = -1;
		m_ReorderDragged = false;
	}
	void MarkReorderDrag() { m_ReorderDragged |= m_ReorderColumn >= 0; }
	bool ReorderDragged() const { return m_ReorderDragged; }
	bool AllowsSort() const { return !m_ReorderDragged && m_ResizeColumn < 0; }
	int ReorderColumn() const { return m_ReorderColumn; }
	int ResizeColumn() const { return m_ResizeColumn; }
};

#endif
