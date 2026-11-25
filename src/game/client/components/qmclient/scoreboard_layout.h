#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_LAYOUT_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SCOREBOARD_LAYOUT_H

#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/scoreboard.h>

inline constexpr float QmScoreboardHeaderFontSize = ui_token::font::SMALL;
inline constexpr float QmScoreboardHeaderButtonHeight = QmScoreboardHeaderFontSize + 2.0f * ui_token::spacing::XS;

inline float QmScoreboardHeaderButtonWidth(float TextWidth)
{
	// 文字宽度、按钮内退和按钮间距同源，避免长标签被单独缩小。
	return std::ceil(TextWidth) + 2.0f * (ui_token::dropdown::EntryInsetX(QmScoreboardHeaderFontSize, QmScoreboardHeaderButtonHeight) + ui_token::spacing::XS) + 1.0f;
}

inline CUIRect QmScoreboardHeaderButtonRect(CUIRect Slot)
{
	Slot.VMargin(ui_token::spacing::XS, &Slot);
	const float Height = std::min(Slot.h, QmScoreboardHeaderButtonHeight);
	Slot.y += (Slot.h - Height) * 0.5f;
	Slot.h = Height;
	return Slot;
}

inline float QmScoreboardTeamBottomPadding(int LastTeam)
{
	return LastTeam > TEAM_FLOCK ? ui_token::spacing::SM : 0.0f;
}

inline SRoundedSurfaceParams QmScoreboardTeamBandSurface(int PreviousTeam, int NextTeam, float Radius)
{
	SRoundedSurfaceParams Surface;
	Surface.m_Corners = IGraphics::CORNER_NONE;
	if(PreviousTeam == TEAM_FLOCK)
		Surface.m_Corners |= IGraphics::CORNER_T;
	if(NextTeam == TEAM_FLOCK)
		Surface.m_Corners |= IGraphics::CORNER_B;
	Surface.m_Radius = Surface.m_Corners == IGraphics::CORNER_NONE ? 0.0f : Radius;
	return Surface;
}

struct SQmScoreboardRowMetrics
{
	float m_LineHeight = 20.0f;
	float m_FontSize = 12.0f;
	float m_TeeSizeMod = 0.4f;
	float m_Spacing = 0.0f;
	float m_RoundRadius = 2.5f;
};

inline SQmScoreboardRowMetrics QmScoreboardPreferredRowMetrics(int NumRows, bool Scroll)
{
	SQmScoreboardRowMetrics Metrics;
	if(!Scroll && NumRows <= 8)
		Metrics = {30.0f, 12.0f, 0.5f, 8.0f, 5.0f};
	else if(!Scroll && NumRows <= 12)
		Metrics = {25.0f, 12.0f, 0.45f, 2.5f, 5.0f};
	else if(!Scroll && NumRows > 16)
		Metrics = {16.0f, 12.0f, 0.25f, 0.0f, 2.5f};
	return Metrics;
}

inline SQmScoreboardRowMetrics QmScoreboardRowMetrics(float AvailableHeight, int NumRows, int NumTeamLabels, int NumTeamModeLabels, bool Scroll)
{
	SQmScoreboardRowMetrics Metrics = QmScoreboardPreferredRowMetrics(NumRows, Scroll);
	const float RequiredHeight = ScoreboardRowsHeight(NumRows, NumTeamLabels, NumTeamModeLabels, Metrics.m_LineHeight, Metrics.m_Spacing, Metrics.m_FontSize / 1.5f, minimum(SCOREBOARD_TEAM_MODE_ICON_SIZE, Metrics.m_FontSize));
	const float FitScale = RequiredHeight > 0.0f ? maximum(0.0f, AvailableHeight) / RequiredHeight : 1.0f;
	const float Scale = Scroll ? 1.0f : minimum(NumRows > 16 ? 1.25f : 1.0f, FitScale);
	Metrics.m_LineHeight *= Scale;
	Metrics.m_FontSize *= Scale;
	Metrics.m_TeeSizeMod *= Scale;
	Metrics.m_Spacing *= Scale;
	Metrics.m_RoundRadius *= Scale;
	// 多人列利用新增纵向空间；字号适度放大后将剩余高度均匀分配给玩家行。
	if(!Scroll && NumRows > 16)
	{
		const float UsedHeight = ScoreboardRowsHeight(NumRows, NumTeamLabels, NumTeamModeLabels, Metrics.m_LineHeight, Metrics.m_Spacing, Metrics.m_FontSize / 1.5f, minimum(SCOREBOARD_TEAM_MODE_ICON_SIZE, Metrics.m_FontSize));
		Metrics.m_LineHeight += maximum(0.0f, AvailableHeight - UsedHeight) / NumRows;
	}
	return Metrics;
}

#endif
