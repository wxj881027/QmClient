#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_BROWSER_COLUMN_LAYOUT_H

#include <algorithm>

struct SQmBrowserColumnLayout
{
	float m_Scale; // 1 = 各列按配置宽度原样渲染；<1 = 溢出，等比收缩
	float m_NameWidth; // 弹性列（名称）最终宽度
};

// 列布局（2026-10-14 重构）：内容列一律按配置的绝对宽度渲染，名称列弹性
// 吸收剩余空间；没有组缩放——拖某一条宽度柄只影响该列与名称列。仅当
// 全部列宽（配置宽 + 名称最小宽）超出可用宽度时整体等比收缩（scale < 1）。
// FixedSum：除名称外全部内容列的配置宽度之和（不含间隙与钉住列）；
inline SQmBrowserColumnLayout QmBrowserColumnLayout(float Available, float FixedSum, float NameMinWidth)
{
	Available = std::max(0.0f, Available);
	FixedSum = std::max(0.0f, FixedSum);
	NameMinWidth = std::max(0.0f, NameMinWidth);

	// 宽裕：固定列原样，名称吸收剩余。
	if(Available >= FixedSum + NameMinWidth)
		return {1.0f, Available - FixedSum};

	// 溢出：全部列使用同一比例，配置下限只约束保存值。
	// 显示时再次逐列钳制下限会突破预算，让末尾列跑出列表。
	const float Budget = FixedSum + NameMinWidth;
	const float Scale = Budget > 0.0f ? std::clamp(Available / Budget, 0.0f, 1.0f) : 1.0f;
	return {Scale, NameMinWidth * Scale};
}

inline float QmBrowserColumnSqueezedWidth(float ConfiguredWidth, float Scale)
{
	return std::max(0.0f, ConfiguredWidth) * Scale;
}

// 表头共享一次按压的所有权；列边缘命中时调宽优先，直到释放不能换成另一种手势。
class CQmBrowserHeaderGesture
{
	int m_ReorderColumn = -1;
	int m_ResizeColumn = -1;

public:
	void Begin(int HeaderColumn, int ResizeColumn)
	{
		if(m_ReorderColumn >= 0 || m_ResizeColumn >= 0)
			return;
		m_ResizeColumn = ResizeColumn;
		m_ReorderColumn = ResizeColumn >= 0 ? -1 : HeaderColumn;
	}
	void End()
	{
		m_ReorderColumn = -1;
		m_ResizeColumn = -1;
	}
	int ReorderColumn() const { return m_ReorderColumn; }
	int ResizeColumn() const { return m_ResizeColumn; }
};

#endif
