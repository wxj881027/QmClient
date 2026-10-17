#ifndef GAME_CLIENT_QMUI_SERVERBROWSERFILTERLAYOUT_H
#define GAME_CLIENT_QMUI_SERVERBROWSERFILTERLAYOUT_H

#include "SettingsPageLayout.h"
#include "SettingsToggleGrid.h"

#include <engine/textrender.h>

#include <algorithm>

inline SSettingsToggleSize MeasureServerBrowserFilterStrictButton(ITextRender &TextRender, const char *pLabel, const SSettingsContentMetrics &Metrics)
{
	const float PaddingX = std::max(Metrics.m_LineSpacing, Metrics.m_BodySize * 0.5f);
	const float PaddingY = Metrics.m_LineSpacing * 0.5f;
	const float IndicatorWidth = Metrics.m_BodySize + Metrics.m_LineSpacing;
	// 紧凑按钮保留一个间距的宽度余量，避免内边距舍入和字形对齐使短标签意外换行。
	const float LabelWidth = TextRender.TextWidth(Metrics.m_BodySize, pLabel) + Metrics.m_LineSpacing;
	auto Size = ResolveSettingsToggleButtonSize(LabelWidth, Metrics.m_BodySize, Metrics.m_LineHeight, PaddingX, PaddingY, IndicatorWidth);
	const CUIRect Label = ResolveSettingsToggleButtonLabel({0, 0, Size.m_Width, Size.m_Height}, PaddingX, PaddingY, IndicatorWidth);
	const float LabelHeight = TextRender.TextBoundingBox(Metrics.m_BodySize, pLabel, -1, Label.w).m_H;
	Size.m_Height = ResolveSettingsToggleButtonSize(0, LabelHeight, Metrics.m_LineHeight, PaddingX, PaddingY).m_Height;
	return Size;
}

#endif // GAME_CLIENT_QMUI_SERVERBROWSERFILTERLAYOUT_H
