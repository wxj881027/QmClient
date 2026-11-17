#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_FOOTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_FOOTER_H

#include <game/client/ui_rect.h>

// 默认主面板向下增加 30，保留底栏 100、面板间隙 5 和屏幕底边 5。
inline float QmScoreboardPanelHeight(float ScreenHeight, float PanelTop)
{
	return minimum(415.0f, maximum(0.0f, ScreenHeight - PanelTop - 110.0f));
}

// 手机滚动模式：面板高度吃满可视区，滚动列表不再挤在默认高度里。
inline float QmScoreboardScrollPanelHeight(float ScreenHeight, float PanelTop)
{
	return minimum(520.0f, maximum(0.0f, ScreenHeight - PanelTop - 30.0f));
}

// 计分板底栏：区块从上往下堆叠，宽度按实测内容收缩并水平居中，不再整宽占位。
// ContentWidth 为该区块内容宽度（已含左右内边距）；返回矩形 w/h 为 0 表示该区块
// 不存在——此时不推进游标，所以空区块不会在底栏中间留下空白。
inline CUIRect QmScoreboardFooterBlock(CUIRect Area, float &CursorY, float ContentWidth, float Height, float Gap)
{
	CUIRect Block{};
	if(ContentWidth <= 0.0f || Height <= 0.0f || Area.w <= 0.0f)
		return Block;
	Block.w = minimum(ContentWidth, Area.w);
	Block.h = Height;
	Block.x = Area.x + (Area.w - Block.w) * 0.5f;
	Block.y = CursorY;
	CursorY = Block.y + Block.h + Gap;
	return Block;
}

// 从 CursorY 起还剩多少高度：旁观者面板按它收缩，任何区块都不越出底栏。
inline float QmScoreboardFooterRemainingHeight(CUIRect Area, float CursorY)
{
	return maximum(0.0f, Area.y + Area.h - CursorY);
}

// 旁观者背景按实际行数收缩，避免人少时留下一大片空面板；
// 行数超过上限时按上限截断，不越出可用高度。
inline float QmScoreboardSpectatorPanelHeight(float AvailableHeight, int LineCount, int MaxLines, float FontSize, float VerticalPadding)
{
	const int VisibleLines = minimum(LineCount, MaxLines);
	return minimum(AvailableHeight, maximum(0, VisibleLines) * FontSize + VerticalPadding);
}

#endif
