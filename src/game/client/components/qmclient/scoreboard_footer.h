#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_FOOTER_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_FOOTER_H

#include <game/client/ui_rect.h>

// 计分板底栏：整宽媒体信息条在上，旁观者列表在下。
struct SQmScoreboardFooterLayout
{
	CUIRect m_Media{};
	CUIRect m_Spectators{};
};

// 媒体条只在有歌曲信息时占位；它和旁观者区域之间保留一个间隙。
// 旁观者区域先拿到可用上限，实际背景高度在文字测量后再收缩。
inline SQmScoreboardFooterLayout QmScoreboardFooterLayout(CUIRect Area, bool HasMedia, bool HasSpectators)
{
	SQmScoreboardFooterLayout Layout;
	if(HasMedia)
	{
		Area.HSplitTop(25.0f, &Layout.m_Media, &Area);
		if(HasSpectators)
			Area.HSplitTop(5.0f, nullptr, &Area);
	}
	if(HasSpectators)
		Layout.m_Spectators = Area;
	return Layout;
}

// 旁观者背景按实际行数收缩，避免人少时留下一大片空面板；
// 行数超过上限时按上限截断，不越出可用高度。
inline float QmScoreboardSpectatorPanelHeight(float AvailableHeight, int LineCount, int MaxLines, float FontSize, float VerticalPadding)
{
	const int VisibleLines = minimum(LineCount, MaxLines);
	return minimum(AvailableHeight, maximum(0, VisibleLines) * FontSize + VerticalPadding);
}

#endif
