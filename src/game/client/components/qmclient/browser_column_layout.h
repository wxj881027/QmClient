#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H

#include <algorithm>
#include <array>
#include <cstddef>

// 列宽始终保持配置值；只平移内容，不用名称列或缩放抵消拖动量。
template<size_t NumColumns>
struct SQmBrowserColumnLayout
{
	std::array<float, NumColumns> m_aLeft{};
	float m_ContentWidth = 0.0f;
	float m_MaxScrollOffset = 0.0f;
	float m_ScrollOffset = 0.0f;
};

template<size_t NumColumns>
inline SQmBrowserColumnLayout<NumColumns> QmBrowserColumnLayout(const std::array<float, NumColumns> &aWidths, float Gap, float ViewportWidth, float ScrollOffset, float MinimumContentWidth = 0.0f)
{
	SQmBrowserColumnLayout<NumColumns> Layout;
	Gap = std::max(0.0f, Gap);
	for(size_t i = 0; i < NumColumns; ++i)
	{
		Layout.m_aLeft[i] = Layout.m_ContentWidth;
		Layout.m_ContentWidth += std::max(0.0f, aWidths[i]);
		if(i + 1 < NumColumns)
			Layout.m_ContentWidth += Gap;
	}
	// 调宽期间可保留尾部空白，避免缩窄末列时滚动钳制把抓住的边缘移走。
	Layout.m_ContentWidth = std::max(Layout.m_ContentWidth, MinimumContentWidth);
	Layout.m_MaxScrollOffset = std::max(0.0f, Layout.m_ContentWidth - std::max(0.0f, ViewportWidth));
	Layout.m_ScrollOffset = std::clamp(ScrollOffset, 0.0f, Layout.m_MaxScrollOffset);
	for(float &Left : Layout.m_aLeft)
		Left -= Layout.m_ScrollOffset;
	return Layout;
}

// 表头共享一次按压的所有权；列边缘命中时调宽优先，直到释放不能换成另一种手势。
class CQmBrowserHeaderGesture
{
	int m_ReorderColumn = -1;
	int m_ResizeColumn = -1;
	bool m_ReorderDragged = false;
	float m_ResizeStartX = 0.0f;
	float m_ResizeStartWidth = 0.0f;
	float m_ResizeScrollOffset = 0.0f;

public:
	void Begin(int HeaderColumn, int ResizeColumn, float MouseX = 0.0f, float Width = 0.0f, float ScrollOffset = 0.0f)
	{
		if(m_ReorderColumn >= 0 || m_ResizeColumn >= 0)
			return;
		m_ReorderDragged = false;
		m_ResizeColumn = ResizeColumn;
		m_ResizeStartX = MouseX;
		m_ResizeScrollOffset = ScrollOffset;
		m_ResizeStartWidth = Width;
		m_ReorderColumn = ResizeColumn >= 0 ? -1 : HeaderColumn;
	}
	int ResizeWidth(float MouseX, int MinWidth, int MaxWidth) const
	{
		const float Width = std::clamp(m_ResizeStartWidth + MouseX - m_ResizeStartX, (float)MinWidth, (float)MaxWidth);
		return (int)(Width + 0.5f);
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
	float ResizeScrollOffset() const { return m_ResizeScrollOffset; }
};

#endif
