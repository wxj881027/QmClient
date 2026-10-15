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
// FixedMinSum：同口径的最小宽度之和（供调用方逐列钳制下限）。
inline SQmBrowserColumnLayout QmBrowserColumnLayout(float Available, float FixedSum, float NameMinWidth)
{
	Available = std::max(0.0f, Available);
	FixedSum = std::max(0.0f, FixedSum);
	NameMinWidth = std::max(0.0f, NameMinWidth);

	// 宽裕：固定列原样，名称吸收剩余。
	if(Available >= FixedSum + NameMinWidth)
		return {1.0f, Available - FixedSum};

	// 溢出：整体等比收缩；固定列由调用方逐列 max(min, 配置宽*scale) 钳制，
	// 名称列收缩到 min*scale（极端窄窗下接受轻微截断）。
	const float Budget = FixedSum + NameMinWidth;
	const float Scale = Budget > 0.0f ? std::clamp(Available / Budget, 0.0f, 1.0f) : 1.0f;
	return {Scale, NameMinWidth * Scale};
}

// 逐列的溢出收缩宽度：max(min, 配置宽 * Scale)。
inline float QmBrowserColumnSqueezedWidth(float ConfiguredWidth, float MinWidth, float Scale)
{
	return std::max(MinWidth, ConfiguredWidth * Scale);
}

#endif
